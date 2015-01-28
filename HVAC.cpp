/*
  HVAC.cpp - Spark specific Arduino library for HVAC control.
  Copyright 2014 Greg Cunningham, CuriousTech.net

  This library is free software; you can redistribute it and/or modify it under the terms of the GNU GPL 2.1 or later.

  This library is distributed in the hope that it will be useful,  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
*/

#include "application.h"
#include "HVAC.h"
#include "math.h"

HVAC::HVAC()
{
	m_bSim = true;                  // Note: Simulation only

	m_EE.fanPostDelay = 90;         // 90 seconds default
	m_EE.filterHours = 0;
	m_EE.cycleMin = 60;             // 60 seconds
	m_EE.cycleMax = 60 * 10;        // 10 minutes
	m_EE.idleMin = 60;              // 60 seconds
	m_EE.cycleThresh = 10;          // 1.0
	m_EE.coolTemp[1] = 810;         // 81.0
	m_EE.coolTemp[0] = 790;
	m_EE.heatTemp[1] = 740;         // 74.0
	m_EE.heatTemp[0] = 700;
	m_EE.eHeatThresh = 30;          // Setting this low (30 deg) for now
	m_EE.id = 0xAA55;               // EE value for validity check or struct size changes

	memset(m_fcData, -1, sizeof(m_fcData)); // invalidate forecast

	m_fcPeaks[0].h = -50;       // set as invalid

	pinMode(P_FAN, OUTPUT);
	pinMode(P_COOL, OUTPUT);
	pinMode(P_REV, OUTPUT);
	pinMode(P_HEAT, OUTPUT);
}

// Switch the fan on/off
void HVAC::fanSwitch(bool bOn)
{
	if(bOn == m_bFanRunning)
		return;

	digitalWrite(P_FAN, bOn ? HIGH:LOW);
	m_bFanRunning = bOn;
	if(bOn)
	{
		m_fanOnTimer = 0;       // reset fan on timer
	}
	else	// fan shut off
	{
        fanTimeAccum();
	}
}

// Accumulate fan running times
void HVAC::fanTimeAccum()
{
	static uint16_t nSecs = 0;

	nSecs += m_fanOnTimer;  // add last run time to total counter
	if(nSecs >= 60 * 60)    // increment filter hours
	{
		m_EE.filterHours++;
		nSecs -= 60*60;     // and subract an hour
	}
}

// Service: called once per second (unless the Spark freezes)
void HVAC::service()
{
	if(m_bFanRunning || m_bRunning)         // furance runs fan seperately
	{
		m_fanOnTimer++;			        	// running time counter

        if(m_fanOnTimer >= 60*60*12)        // 12 hours, add up and reset
        {
            fanTimeAccum();
            m_fanOnTimer = 0;
        }
	}

	if(m_fanPostTimer)		        		// Fan conintuation delay
	{
		if(--m_fanPostTimer == 0)
			if(!m_bRunning)			        // Ensure system isn't running
				fanSwitch(false);
	}

	if(m_bRunning)
	{
        m_runTotal++;
		if(++m_cycleTimer < 20)		        // Block changes for at least 20 seconds
			return;
        if(m_cycleTimer >= m_EE.cycleMax)   // running too long (todo: skip for eHeat?)
            m_bStop = true;
	}
	else
	{
		m_idleTimer++;		        		// Time since stopped
	}

	if(m_setMode != m_EE.Mode)		    	// requested HVAC mode change
	{
        if(m_bRunning)                      // cycleTimer is already > 20s here
    		m_bStop = true;
        if(m_idleTimer >= 5)
    	{
    	    m_EE.Mode = m_setMode;	    	// User may be cycling through modes (give 5s)
            calcTargetTemp(m_EE.Mode);
        }
	}
	if(m_bStart && !m_bRunning)	            // Start signal occurred
	{
        int8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode;

		m_bStart = false;

		switch(mode)
		{
			case Mode_Cool:
				fanSwitch(true);
				digitalWrite(P_COOL, HIGH);
				break;
			case Mode_Heat:
                if(m_heatMode)  // gas
                {
    				fanSwitch(false);
	    			digitalWrite(P_HEAT, HIGH);
                }
                else
                {
    				fanSwitch(true);
    				digitalWrite(P_REV, HIGH);
    				digitalWrite(P_COOL, HIGH);
                }
				break;
		}
		m_bRunning = true;
		m_cycleTimer = 0;
		m_startingTemp = m_inTemp;
        m_startingRh = m_rh;
		Serial.print("Op started. inTemp=");
		Serial.println((float)m_inTemp/10);
	}

	if(m_bStop && m_bRunning)	        		// Stop signal occurred
	{
		m_bStop = false;
		digitalWrite(P_REV, LOW);
		digitalWrite(P_COOL, LOW);
		digitalWrite(P_HEAT, LOW);

        if(m_bFanRunning && m_bFanMode == false) // Note: furance manages fan
        {
    		if(m_EE.fanPostDelay)			    // leave fan running to circulate air longer
        		m_fanPostTimer = m_EE.fanPostDelay;
        	else
	        	fanSwitch(false);
        }

        if(m_heatMode)                  // count run time as fan time in winter
        {
            m_fanOnTimer += 60;         // furnace post fan is 60 seconds
            fanTimeAccum();
        }
        
		m_bRunning = false;
		m_idleTimer = 0;
		analyze();
	}

	if(m_bSim)
		simulator();

	tempCheck();
}

// Control switching of system by temp
void HVAC::tempCheck()
{
	if(m_inTemp == 0)           	// hasn't been set yet (todo: validate m_forecastTemp[0])
		return;

	if(m_EE.Mode == Mode_Off)		// nothing to do
		return;

	if(m_bRunning)
	{
		if(m_cycleTimer < m_EE.cycleMin)
			return;

        int8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode;

        if(Time.second() == 0)
            preCalcCycle(m_EE.Mode);
    
		switch(mode)
		{
			case Mode_Cool:
				if( m_inTemp <= m_targetTemp ) // has cooled to desired temp
					m_bStop = true;
				break;
			case Mode_Heat:
				if(m_inTemp >= m_targetTemp ) // has heated to desired temp
					m_bStop = true;
				break;
		}
	}
	else	// not running
	{
		if(m_idleTimer < m_EE.idleMin)
			return;

        if(Time.second() == 0)
    		m_bStart = preCalcCycle(m_EE.Mode);
	}
}

bool HVAC::preCalcCycle(int8_t mode)
{
	int16_t diff = m_inTemp - m_outTemp;            // indoor/outdoor difference

    bool bRet = false;

    // Standard triggers for now
    switch(mode)
    {
        case Mode_Cool:
            calcTargetTemp(Mode_Cool);
            if(m_inTemp >= m_targetTemp + m_EE.cycleThresh)    // has reached threshold above desired temp
			    bRet = true;
            break;
        case Mode_Heat:
            calcTargetTemp(Mode_Heat);
			if(m_inTemp <= m_targetTemp - m_EE.cycleThresh)
				bRet = true;
            break;
        case Mode_Auto:
			if(m_inTemp >= m_EE.coolTemp[0] + m_EE.cycleThresh)
			{
                calcTargetTemp(Mode_Cool);
				m_AutoMode = Mode_Cool;
				bRet = true;
			}
			else if(m_inTemp <= m_EE.heatTemp[1] - m_EE.cycleThresh)
			{
                calcTargetTemp(Mode_Heat);
			    if(m_inTemp < m_outTemp - (m_EE.eHeatThresh * 10) )    // Use gas when efficiency too low for pump
				    m_heatMode = 1;
                else
				    m_heatMode = 0;
			    bRet = true;
			}
            break;
    }

    if(bRet)
    {
        Serial.println("preCalc");

        if(diff < 0)        // Cooling is required at around 30 under indoor
        {
            diff = -diff;
            Serial.print(" Cool i/o diff=");
            Serial.print( (float)diff / 10 );
            Serial.print(", deg to stop=");
            Serial.println( (float)(m_inTemp - m_targetTemp) / 10 );
        }
        else                // Heating is required at around 25 under indoor    (testing)
        {                   // Note: If outdoor temp climbs from 30 to 50, indoor temp doesn't drop from 70.
            Serial.print(" Heat i/o diff=");
            Serial.print( (float)diff / 10 );
            Serial.print(", deg to stop=");
            Serial.println( (float)(m_targetTemp - m_inTemp) / 10 );
        }
    }

    return bRet;
}

void HVAC::calcTargetTemp(int8_t mode)
{
//  int16_t futDiff = m_fcData[1].t - m_outTemp;    // difference between now and up to 3 hours ahead
//  bool bIncreasing = futDiff > 0;

    Serial.println(Time.timeStr());
    Serial.print("Peaks ");
    Serial.print(m_fcPeaks[0].t);
    Serial.print(" at ");
    Serial.print(m_fcPeaks[0].h);
    Serial.print(", ");
    Serial.print(m_fcPeaks[1].t);
    Serial.print(" at ");
    Serial.print(m_fcPeaks[1].h);
    Serial.print(", ");
    Serial.print(m_fcPeaks[2].t);
    Serial.print(" at ");
    Serial.println(m_fcPeaks[2].h);

    if(m_fcPeaks[0].h == -50)   // not initialized yet
    {
        Serial.println("Not init");
        return;
    }
    
    int8_t h = Time.hour();

    if(h >= m_fcPeaks[1].h)        // Shift over
    {
        Serial.println("Time shift");
        m_fcPeaks[0] = m_fcPeaks[1];
        m_fcPeaks[1] = m_fcPeaks[2];
        m_fcPeaks[2].h = m_fcPeaks[2].t = -1;       // invalidate
        if(m_fcPeaks[0].h > 24)                     // adjust time 1 day
            m_fcPeaks[0].h -= 24;
    }

    bool bLowHigh = (m_fcPeaks[0].t < m_fcPeaks[1].t);   // true = low->high, false = high->low

    int8_t hrs = m_fcPeaks[1].h - m_fcPeaks[0].h;

    int m = Time.minute();

    if(m_fcPeaks[0].h <= h)
        m += (h - m_fcPeaks[0].h) * 60; // offset into first peak
    else
        m = 0;

    switch(mode)
    {
        case Mode_Cool:
            if( m_outTemp < m_outMin * 10 )    // Todo: Fix the peak error
            {
                Serial.println("Error: outTemp range low");
                m_targetTemp = m_EE.coolTemp[0];
            }
            else if( m_outTemp > m_outMax * 10 )
            {
                Serial.println("Error: outTemp range high");
                m_targetTemp = m_EE.coolTemp[1];
            }else
            {
            	m_targetTemp  = scaleRange( m_EE.coolTemp[0], m_EE.coolTemp[1]);
            }
            break;
        case Mode_Heat:
            if( m_outTemp < m_outMin * 10 )
            {
                Serial.println("Error: outTemp range low");
                m_targetTemp = m_EE.heatTemp[0];
            }
            else if( m_outTemp > m_outMax * 10 )
            {
                Serial.println("Error: outTemp range high");
                m_targetTemp = m_EE.heatTemp[1];
            }else
            {
            	m_targetTemp  = scaleRange( m_EE.heatTemp[0], m_EE.heatTemp[1]);
            }
            break;
    }

    Serial.print("Range LH=");
    Serial.print(bLowHigh);
    Serial.print(" hrs=");
    Serial.print(hrs);
    Serial.print(" target=");
    Serial.println(m_targetTemp);
}

// scale target temp of inside range to current out temp of outside range
int HVAC::scaleRange(uint16_t inL, uint16_t inH)
{
    if(m_outTemp > m_outMax * 10)  return inH;
    if(m_outTemp < m_outMin * 10)  return inL;

        Serial.print("scale ");
        Serial.print(inL);
        Serial.print(" ");
        Serial.print(inH);
        Serial.print(" ");
        Serial.print(m_outMin);
        Serial.print(" ");
        Serial.print(m_outMax);
        Serial.print(" ");
        Serial.println(m_outTemp);

    return (m_outTemp-(m_outMin*10)) * (inH-inL) / ((m_outMax*10)-(m_outMin*10)) + inL;
}

// Analyze temp change over a cycle and log it
void HVAC::analyze()
{
	int16_t Diff = m_inTemp - m_startingTemp;

	if(Diff < 0) Diff = -Diff;

    m_tempDiffTotal += Diff;

	Serial.println("Cycle complete.");
//	Serial.print("Temp chg = ");
//	Serial.println( (float)Diff / 10 );
//	Serial.print("Time taken = ");
//	Serial.print(m_cycleTimer / 60);
//	Serial.print("m ");
//	Serial.print(m_cycleTimer % 60);
//	Serial.println("s");

    int i;
    for(i = 0; i < 32; i++)         // find empty slot
    {
        if(m_logs[i].time == 0)
            break;
    }
    if(i < 32)
    {
        m_logs[i].time = Time.now();
        m_logs[i].mode = m_EE.Mode;
        if(m_EE.Mode == Mode_Auto)
            m_logs[i].mode |= (m_AutoMode << 4);
        m_logs[i].secs = m_cycleTimer;
        m_logs[i].t1 = m_startingTemp;
        m_logs[i].rh1 = m_startingRh;
        m_logs[i].t2 = m_inTemp;
        m_logs[i].rh2 = m_rh;
    }
}

bool HVAC::getRunning()
{
    return m_bRunning;
}

int8_t HVAC::getMode()
{
    return m_EE.Mode;
}

void HVAC::setHeatMode(uint8_t mode)
{
    m_heatMode = mode;
}

uint8_t HVAC::getHeatMode()
{
    return m_heatMode;
}

int8_t HVAC::getAutoMode()
{
    return m_AutoMode;
}

int8_t HVAC::getSetMode()
{
    return m_setMode;
}

// User:Set a new control mode
void HVAC::setMode(int8_t mode)
{
	m_setMode = mode & 3;
    if(!m_bRunning)
        m_idleTimer = 0;        // keep it from starting too quickly
}

bool HVAC::getFan()
{
    return m_bFanMode;
}

// User:Set fan mode
void HVAC::setFan(bool on)
{
	if(on == m_bFanMode)			// requested fan operating mode change
		return;

	m_bFanMode = on;
	if(!m_bRunning)
		fanSwitch(on);              // manual fan on/off if not running
}

int16_t HVAC::getSetTemp(int8_t mode, int8_t hl)
{
	switch(mode)
	{
		case Mode_Cool:
			return m_EE.coolTemp[hl];
		case Mode_Heat:
			return m_EE.heatTemp[hl];
		case Mode_Auto:
			return (m_AutoMode == Mode_Cool) ? m_EE.coolTemp[hl] : m_EE.heatTemp[hl];
	}
    return 0;
}

// User:Set new control temp
void HVAC::setTemp(int8_t mode, int16_t Temp, int8_t hl)
{
    if(mode == Mode_Auto)
    {
        mode = m_AutoMode;
    }

    int8_t save;

	switch(mode)
	{
		case Mode_Cool:
            if(Temp < 650 || Temp > 880)    // ensure sane values
                break;
			m_EE.coolTemp[hl] = Temp;
            if(hl)
            {
                m_EE.coolTemp[0] = min(m_EE.coolTemp[1], m_EE.coolTemp[0]);     // don't allow h/l to invert
	        }
            else
            {
                m_EE.coolTemp[1] = max(m_EE.coolTemp[0], m_EE.coolTemp[1]);
	        }
            save = m_EE.heatTemp[1] - m_EE.heatTemp[0];
			m_EE.heatTemp[1] = min(m_EE.coolTemp[0] - 20, m_EE.heatTemp[1]); // Keep 2.0 degree differencial for Auto mode
            m_EE.heatTemp[0] = m_EE.heatTemp[1] - save;                      // shift heat low by original diff

            if(m_EE.Mode == Mode_Cool)
                calcTargetTemp(m_EE.Mode);

			break;
		case Mode_Heat:
            if(Temp < 630 || Temp > 860)    // ensure sane values
                break;
			m_EE.heatTemp[hl] = Temp;
            if(hl)
            {
                m_EE.heatTemp[0] = min(m_EE.heatTemp[1], m_EE.heatTemp[0]);
	        }
            else
            {
                m_EE.heatTemp[1] = max(m_EE.heatTemp[0], m_EE.heatTemp[1]);
	        }
            save = m_EE.coolTemp[1] - m_EE.coolTemp[0];
			m_EE.coolTemp[0] = max(m_EE.heatTemp[1] - 20, m_EE.coolTemp[0]);
            m_EE.coolTemp[1] = m_EE.coolTemp[0] + save;

            if(m_EE.Mode == Mode_Heat)
                calcTargetTemp(m_EE.Mode);

			break;
	}
}

// Update when DHT22 changes
void HVAC::updateIndoorTemp(int16_t Temp, int16_t rh)
{
	if(m_inTemp != 0 && m_bSim)	// get real temp once if simulating
		return;
	m_inTemp = Temp;
	m_rh = rh;
}

// Update outdoor temp
void HVAC::updateOutdoorTemp(int16_t outTemp)
{
    m_outTemp = outTemp;
}

// Update min/max for next 24 hrs
void HVAC::updatePeaks(int8_t min, int8_t max)
{
    m_outMin = min;
    m_outMax = max;

    Forecast fcL, fcH, fcTemp;
    fcL.t = 126;
    fcH.t = -50;

    // Get low/high for next 24 hours  Todo: change to AM/PM fixed peak
    for(int8_t i = 0; i < 8; i++)  // 8*3 = 24
    {
        int8_t t = m_fcData[i].t;
        if(fcL.t > t)
            fcL = m_fcData[i];
        if(fcH.t < t)
            fcH = m_fcData[i];
    }

    fcTemp = m_fcPeaks[1];

    m_fcPeaks[1] = (fcL.h < fcH.h) ? fcL : fcH; // time ordered
    m_fcPeaks[2] = (fcL.h > fcH.h) ? fcL : fcH;

    if(m_fcPeaks[0].h == -50)        // fake the first entry
    {
        m_fcPeaks[0].h = Time.hour();
        m_fcPeaks[0].t = m_fcData[0].t;
    }
    else
    {
        if( (fcTemp.h+2) <= Time.hour() )   // updates are every 3 hours
            m_fcPeaks[0] = fcTemp;          // make sure old peak isn't lost

        if(fcTemp.h <= m_fcPeaks[0].h)      // this shouldn't happen
            m_fcPeaks[0] = fcTemp;

        if(m_fcPeaks[0].h > m_fcPeaks[1].h) // temporary
            m_fcPeaks[0].h -= 24;

        if(m_fcPeaks[0].h > Time.hour()) // fix peak when midnight rolls
            m_fcPeaks[0].h -= 24;
    }
}

void HVAC::resetFilter()
{
	m_EE.filterHours = 0;
}

bool HVAC::checkFilter(void)
{
    return (m_EE.filterHours >= 200);
}

void HVAC::resetTotal()
{
	m_runTotal = 0;
	m_tempDiffTotal = 0;
}

int HVAC::CmdIdx(String s, const char **pCmds )
{
    int iCmd;

    for(iCmd = 0; pCmds[iCmd]; iCmd++)
    {
        if( s.equalsIgnoreCase( String(pCmds[iCmd]) ) )
            break;
    }
    return iCmd;
}

static const char *cGCmds[] =
{
    "interface",
	"settings",
	"temp",
    "log",
    NULL
};

// Spark cloud vars
int HVAC::getVar(String s)
{
	int r = 0;
    int i;

	switch(CmdIdx(s, cGCmds))
	{
        case 0:     // interface
            r = 'thst';//0x74687374;                 // unique response = thermostat
            break;
		case 1:    // settings
			r = m_EE.Mode;			    	// 2
			r |= (m_AutoMode << 2);		    // 2
			r |= (m_heatMode << 3);	        // 1
			r |= (m_bFanMode ? 0x20:0); 	// 1
			r |= (m_bRunning ? 0x40:0); 	// 1
			r |= (m_bFanRunning ? 0x80:0);  // 1
			r |= m_EE.eHeatThresh << 8;     // 6 (63 max)
			r |= m_EE.fanPostDelay << 14;	// 8 (255)
			r |= m_EE.cycleThresh << 22; 	// 8
            sprintf(m_szResult, "{\"c0\":%d,\"c1\":%d,\"h0\":%d,\"h1\":%d,\"it\":%d,\"tt\":%d,\"im\":%d,\"cn\":%d,\"cx\":%d,\"fh\":%d,\"ot\":%d,\"ol\":%d,\"oh\":%d,\"ct\":%d,\"ft\":%d,\"rt\":%d,\"td\":%d}",
            // 138-170
                m_EE.coolTemp[0], m_EE.coolTemp[1], m_EE.heatTemp[0], m_EE.heatTemp[1],
                m_inTemp, m_targetTemp, m_EE.idleMin, m_EE.cycleMin, m_EE.cycleMax, m_EE.filterHours,
                m_outTemp, m_outMin, m_outMax, m_cycleTimer, m_fanOnTimer, m_runTotal, m_tempDiffTotal);
			break;
        case 2: // temp
			r = m_inTemp;		            //
            r |= m_rh << 10;
            r |= m_outTemp << 20;
			break;	// <16 bits
        case 3:     // log
            for(i = 0; i < 32; i++)         // count how many left for return value
                if(m_logs[i].time)
                    r++;
            if(r)
            {
                for(i = 0; i < 32; i++)     // find next unsent and set it up for request
                    if(m_logs[i].time)
                        break;
                sprintf(m_szResult, "{\"time\":%d,\"mode\":%d,\"secs\":%d,\"t1\":%d,\"rh1\":%d,\"t2\":%d,\"rh2\":%d}",
                        m_logs[i].time, m_logs[i].mode, m_logs[i].secs, m_logs[i].t1, m_logs[i].rh1, m_logs[i].t2, m_logs[i].rh2);
                m_logs[i].time = 0;
            }
            break;
	}
	return r;
}

static const char *cSCmds[] =
{
    "fanmode",
    "mode",
    "heatmode",
    "resettotal",
    "resetfilter",
    "fanpostdelay",
    "cyclemin",
    "cyclemax",
    "idlemin",
    "cyclethresh",
    "cooltempl",
    "cooltemph",
    "heattempl",
    "heattemph",
    "eheatthresh",
    "override",
    "notify",
    NULL
};

// Spark cloud vars (warning: returns button # to refresh)
int HVAC::setVar(String s)
{
    int off = s.indexOf(',');

    if(off < 0)     // deny if no value
        return -1;

    String sVal = s.substring(off + 1);
    int val = sVal.toInt();

	switch( CmdIdx( s.substring(0, off), cSCmds ) )
	{
		case 0:     // fanmode
            setFan( (val) ? true:false);
            return 8;
		case 1:     // mode
            setMode( val );
            return 9;
		case 2:     // heatmode
            setHeatMode( val );
            return 10;
		case 3:     // resettotal
			resetTotal();
			break;
        case 4:
            resetFilter();
            return 11;
		case 5:     // fanpostdelay
			m_EE.fanPostDelay = val;
			break;
		case 6:     // cyclenmin
			m_EE.cycleMin = val;
			break;
		case 7:     // cyclemax
			m_EE.cycleMax = val;
			break;
		case 8:     // idlemin
			m_EE.idleMin = val;
			break;
		case 9:    // cyclethresh
			m_EE.cycleThresh = val;
			break;
		case 10:    // cooltempl
            setTemp(Mode_Cool, val, 0);
            return 1;
		case 11:    // cooltemph
            setTemp(Mode_Cool, val, 1);
            return 0;
		case 12:    // heattempl
            setTemp(Mode_Heat, val, 0);
            return 3;
		case 13:    // heattemph
            setTemp(Mode_Heat, val, 1);
            return 2;
		case 14:    // eheatthresh
            m_EE.eHeatThresh = val;
		    break;
        case 15:    // override
            m_targetTemp = val;
            if(!m_bRunning)
            {
                // todo: check mode/temp, set to AutoMode if not correct
                m_bStart = true;
            }
            break;
        case 16:    // notify
            {
                static char szNote[16];
                sVal.toCharArray( szNote, 16);
                addNotification(szNote);
            }
            return 11;
	}
	return -1;      // default no button to refresh
}

bool HVAC::addNotification(const char *pszText)
{
    int i;
    
    for(i = 0; i < 8; i++)
        if(!m_pszNote[i])
            break;
    if(i < 8)
    {
        m_pszNote[i] = pszText;
        return true;
    }
    return false;
}

void HVAC::clearNotification(int n)
{
    if( n >= 0 && n < 8)
        m_pszNote[n] = NULL;
}

// Simulate indoor temp changes
void HVAC::simulator()
{
	static int8_t nCount = 0;

	if(++nCount < 30)		// starting out with 30 seconds
		return;
	nCount = 0;

	int Diff = m_inTemp - m_outTemp;	// difference in/out

	if(Diff < 0) Diff = -Diff;

	if(Diff < 10)           // <1.0 = no change range
		return;

	if(m_bRunning)
	{
//        int Chg = 1;    // 0.1
//    	if(Diff < 200)  // 20deg
//    		Chg = -(int)(log(Diff * 0.01));

//		Serial.print("Sim:");

		switch(m_EE.Mode)
		{
			case Mode_Cool:
				m_inTemp--;
  //      		Serial.print("Cool");
				break;
			case Mode_Heat:
				m_inTemp++;
//        		Serial.print("Heat");
				break;
			case Mode_Auto:
//        		Serial.print("Auto ");
//        		Serial.print(m_AutoMode);
				if(m_AutoMode == Mode_Cool) m_inTemp--;
				if(m_AutoMode == Mode_Heat) m_inTemp++;
				break;
		}

//		Serial.print(" inTemp = ");
//		Serial.print((float)m_inTemp / 10);
//		Serial.print(" Chg = ");
//		Serial.println((float)Chg / 10);
	}
	else	// not running
	{
//	    int Chg = (int)(exp( Diff * 0.005 ));
        static int8_t skip = 0;

        if(++skip > 2)                      // slower change when off
        {
    		if(m_inTemp - m_outTemp > 0)	// colder outside
    			m_inTemp--;
    		else	// warmer outside
    			m_inTemp++;
            skip = 0;
        }
//		Serial.print("Sim:Off inTemp = ");
//		Serial.print((float)m_inTemp / 10);
//		Serial.print(" Chg = ");
//		Serial.println((float)Chg / 10);
	}
}
