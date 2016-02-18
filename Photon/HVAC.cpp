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

#define FF_DELAY 120            // internal furnace fan post-run delay

HVAC::HVAC()
{
	m_EE.filterMinutes = 0;
	m_EE.cycleMin = 60;		    // 60 seconds minimum for a cycle
	m_EE.cycleMax = 60*15;		// 15 minutes maximun for a cycle
	m_EE.idleMin = 60*5;		// 5 minutes minimum between cycles
	m_EE.cycleThresh = 16;		// 1.6 degree cycle range
	m_EE.coolTemp[1] = 820;		// 82.0 default temps
	m_EE.coolTemp[0] = 790;		// 79.0
	m_EE.heatTemp[1] = 740;		// 74.0
	m_EE.heatTemp[0] = 700;		// 70.0
	m_EE.eHeatThresh = 30;		// Setting this low (30 deg) for now
	m_EE.fanPostDelay[0] = 60;  // 1 minute after compressor stops (HP)
	m_EE.fanPostDelay[1] = 120; // 2 minutes after compressor stops (cool)
	m_EE.overrideTime = 10 * 60;	// setting for override
	m_EE.id = 0xAA55;		    // EE value for validity check or struct size changes
	memset(m_fcData, -1, sizeof(m_fcData)); // invalidate forecast

//	m_fcPeaks[0].h = -50		// set as invalid

	m_outMax[0] = -50;	    	// set as invalid
	m_outMax[1] = -50;	    	// set as invalid

    m_remoteTimeout = 60*5;    // 5 minutes default

	pinMode(P_FAN, OUTPUT);
	pinMode(P_COOL, OUTPUT);
	pinMode(P_REV, OUTPUT);
	pinMode(P_HEAT, OUTPUT);
	digitalWrite(P_HEAT, LOW);
	digitalWrite(P_REV, LOW);
	digitalWrite(P_COOL, LOW);
	digitalWrite(P_FAN, LOW);

	m_idleTimer = 1*60;            // start with a high idle, in case of power outage
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
}

// Accumulate fan running times
void HVAC::filterInc()
{
	static uint16_t nSecs = 0;

	nSecs ++;  // add last run time to total counter
	if(nSecs >= 60)    // increment filter minutes
	{
		m_EE.filterMinutes++;
		nSecs -= 60;     // and subtract a minute
	}
}

// Shut off when WiFi fails.  Hopefully before it goes to breathing green
void HVAC::disable()
{
	digitalWrite(P_HEAT, LOW);
	digitalWrite(P_COOL, LOW);
	digitalWrite(P_FAN, LOW);
	m_bRunning = false;
	m_bEnabled = false;
}

// Service: called once per second (unless the Spark freezes)
void HVAC::service()
{
	if(m_bFanRunning || m_bRunning || m_furnaceFan)  // furance runs fan seperately
	{
		filterInc();
		m_fanOnTimer++;			        	// running time counter

		if(m_fanOnTimer >= 60*60*12)        // 12 hours, add up and reset
		{
			m_fanOnTimer = 0;
		}
        if(m_furnaceFan)                       // fake fan status for furnace fan
        {
            m_furnaceFan--;
        }
	}

	if(m_fanPostTimer)		        		// Fan conintuation delay
	{
		if(--m_fanPostTimer == 0)
			if(!m_bRunning && m_bFanMode == false) // Ensure system isn't running and fanMode is auto
				fanSwitch(false);
	}

    if(m_remoteTimer)                       // remote temperature override timer
    {
        m_remoteTimer--;
    }

	if(m_overrideTimer)
	{
		if(--m_overrideTimer == 0)
		{
		    m_ovrTemp = 0;
			calcTargetTemp(m_EE.Mode);
		}
	}

	if(m_bRunning)
	{
		m_runTotal++;
		if(++m_cycleTimer < 20)		        // Block changes for at least 20 seconds
			return;
		if(m_cycleTimer >= m_EE.cycleMax)   // running too long (todo: skip for eHeat?)
		{
            m_bStop = true;
            Particle.publish("pushbullet", "SparkOStat Cycle limit hit", 60, PRIVATE);
        }
	}
	else
	{
		m_idleTimer++;		        		// Time since stopped
	}

	if(m_setMode != m_EE.Mode || m_setHeat != m_EE.heatMode)   	// requested HVAC mode change
	{
		if(m_bRunning)                      // cycleTimer is already > 20s here
			m_bStop = true;
		if(m_idleTimer >= 5)
		{
            m_EE.heatMode = m_setHeat;
			m_EE.Mode = m_setMode;	        // User may be cycling through modes (give 5s)
			calcTargetTemp(m_EE.Mode);
		}
	}

	int8_t hm = (m_EE.heatMode == Heat_Auto) ? m_AutoHeat : m_EE.heatMode;
	int8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode;

	if(m_bStart && !m_bRunning)	            // Start signal occurred
	{
		m_bStart = false;

		switch(mode)
		{
			case Mode_Cool:
				fanSwitch(true);
				if(digitalRead(P_REV) != HIGH)
				{
					digitalWrite(P_REV, HIGH);  // set heatpump to cool (if heats, reverse this)
					delay(5000);                //    if no heatpump, remove
				}
				digitalWrite(P_COOL, HIGH);
				break;
			case Mode_Heat:
				if(hm)  // gas
				{
					digitalWrite(P_HEAT, HIGH);
				}
				else
				{
					fanSwitch(true);
					if(digitalRead(P_REV) != LOW)  // set heatpump to heat (if heats, reverse this)
					{
						digitalWrite(P_REV, LOW);
						delay(5000);
					}
					digitalWrite(P_COOL, HIGH);
				}
				break;
		}
		m_bRunning = true;
		m_cycleTimer = 0;
		m_startingTemp = m_inTemp;
		m_startingRh = m_rh;
//		Serial.print("Op started. inTemp=");
//		Serial.println(m_inTemp);
	}

	if(m_bStop && m_bRunning)	        		// Stop signal occurred
	{
		m_bStop = false;
		digitalWrite(P_COOL, LOW);
		digitalWrite(P_HEAT, LOW);

		if(m_bFanRunning && m_bFanMode == false) // Note: furance manages fan
		{
			if(m_EE.fanPostDelay[digitalRead(P_REV)])			    // leave fan running to circulate air longer
				m_fanPostTimer = m_EE.fanPostDelay[digitalRead(P_REV)]; // P_REV == true if cooling
			else
				fanSwitch(false);
		}
		
		if(mode == Mode_Heat && hm)    // count run time as fan time in winter
		{			         // furnace post fan is 120 seconds
			m_furnaceFan = FF_DELAY;
		}
        
		analyze();
		m_bRunning = false;
		m_idleTimer = 0;
	}

	tempCheck();

    setValues();

    if(m_pubTimer)
    {
        if(--m_pubTimer == 0)
        {
            m_pubTimer = m_pubTime;
            pushData();
        }
    }
}

// Control switching of system by temp
void HVAC::tempCheck()
{
	if(m_inTemp == 0 || m_bEnabled == false)   	// hasn't been set yet
		return;

	if(m_EE.Mode == Mode_Off)		// nothing to do
		return;

	if(m_bRunning)
	{
		if(m_cycleTimer < m_EE.cycleMin)
			return;

		int8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode;

		if(Time.second() == 0 || m_bRecheck)
		{
			m_bRecheck = false;
			preCalcCycle(m_EE.Mode);
		}

		switch(mode)
		{
			case Mode_Cool:
				if( m_inTemp <= m_targetTemp - m_EE.cycleThresh ) // has cooled to desired temp - threshold
					m_bStop = true;
				break;
			case Mode_Heat:
				if(m_inTemp > m_targetTemp + m_EE.cycleThresh) // has heated above desired temp + threshold
					m_bStop = true;
				break;
		}
	}
	else	// not running
	{
		if(m_idleTimer < m_EE.idleMin)
			return;

		if(Time.second() == 0 || m_bRecheck)
		{
		    m_bRecheck = false;
		    m_bStart = preCalcCycle(m_EE.Mode);
		}
	}
}

bool HVAC::preCalcCycle(int8_t mode)
{
//	int16_t diff = m_inTemp - m_outTemp;            // indoor/outdoor difference

	bool bRet = false;
	
	// Standard triggers for now
	switch(mode)
	{
		case Mode_Cool:
			calcTargetTemp(Mode_Cool);
			if(m_inTemp >= m_targetTemp)    // has reached threshold above desired temp
				bRet = true;
			break;
		case Mode_Heat:
			calcTargetTemp(Mode_Heat);
			if(m_inTemp <= m_targetTemp)
				bRet = true;
			break;
		case Mode_Auto:
			if(m_inTemp >= m_EE.coolTemp[0])
			{
//				Serial.print("Auto cool ");
//				Serial.print(m_inTemp);
//				Serial.print(" ");
//				Serial.println(m_EE.coolTemp[0]);
				calcTargetTemp(Mode_Cool);
				m_AutoMode = Mode_Cool;
				bRet = true;
			}
			else if(m_inTemp <= m_EE.heatTemp[1])
			{
//				Serial.println("Auto heat");
				m_AutoMode = Mode_Heat;
				calcTargetTemp(Mode_Heat);
				if(m_EE.heatMode == Heat_Auto)
				{
					if(m_inTemp < m_outTemp - (m_EE.eHeatThresh * 10)) 	// Use gas when efficiency too low for pump
						m_AutoHeat = Heat_NG;
					else
						m_AutoHeat = Heat_HP;
				}
				bRet = true;
			}
			break;
	}
/*
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
*/
	return bRet;
}

void HVAC::calcTargetTemp(int8_t mode)
{
	if(!m_bRunning)
		if(digitalRead(P_REV) != (mode == Mode_Cool) )  // set heatpump same as current mode (Note: some models may be reversed)
			digitalWrite(P_REV, (mode == Mode_Cool) ? HIGH : LOW);

	int16_t L = m_outMin[1];
	int16_t H = m_outMax[1];
	
	if(m_outMax[0] != -50)  // Use longer range if available
	{
		L = min(m_outMin[0], L);
		H = max(m_outMax[0], H);
	}
	
	L *= 10;    // shift a decimal place
	H *= 10;

	switch(mode)
	{
		case Mode_Cool:
			if( m_outTemp < L )    // Todo: Fix the peak error
			{
			//	Serial.println("Error: outTemp range low");
				m_targetTemp = m_EE.coolTemp[0];
			}
			else if( m_outTemp > H )
			{
			//	Serial.println("Error: outTemp range high");
				m_targetTemp = m_EE.coolTemp[1];
			}else
			{
				m_targetTemp  = (m_outTemp-L) * (m_EE.coolTemp[1]-m_EE.coolTemp[0]) / (H-L) + m_EE.coolTemp[0];
			}
			break;
		case Mode_Heat:
			if( m_outTemp < L )
			{
			//	Serial.println("Error: outTemp range low");
				m_targetTemp = m_EE.heatTemp[0];
			}
			else if( m_outTemp > H )
			{
			//	Serial.println("Error: outTemp range high");
				m_targetTemp = m_EE.heatTemp[1];
			}else
			{
				m_targetTemp  = (m_outTemp-L) * (m_EE.heatTemp[1]-m_EE.heatTemp[0]) / (H-L) + m_EE.heatTemp[0];
			}
			break;
	}
	m_targetTemp += m_ovrTemp; // override is normally 0, unless set remotely with a timeout
//	Serial.print(" target=");
//	Serial.println(m_targetTemp);
}

// Analyze temp change over a cycle and log it
void HVAC::analyze()
{
	int16_t Diff = m_inTemp - m_startingTemp;

	if(Diff < 0) Diff = -Diff;

	m_tempDiffTotal += Diff;

    char szLog[128];
	sprintf(szLog, "{\"time\":%d,\"st\":%d,\"secs\":%d,\"t1\":%d,\"rh1\":%d,\"t2\":%d,\"rh2\":%d}",
		Time.now(), getState(), m_cycleTimer, m_startingTemp, m_startingRh, m_inTemp, m_rh);

    Particle.publish("hvacLog", szLog, 60, PRIVATE);
}

uint8_t HVAC::getState()
{
    if( m_bRunning == false) return 0;

    // Check if NG furnace is running, which controls the fan automatically
	uint8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode; // convert auto to just cool / heat

	if(mode == Mode_Heat && ( m_EE.heatMode == Heat_NG || (m_EE.heatMode == Heat_Auto && m_AutoHeat == Heat_NG) ) )  // convert any NG mode to 3
		mode = 3; // so logs will only be 1, 2 or 3.

	return mode;
}

bool HVAC::getFanRunning()
{
	bool bOn = m_bFanRunning;

    // Check if NG furnace is running, which controls the fan automatically
	uint8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode; // convert auto to just cool / heat

    if(mode == Mode_Heat && ( m_EE.heatMode == Heat_NG || (m_EE.heatMode == Heat_Auto && m_AutoHeat == Heat_NG) )) // if heat is nat gas
    {
        if(m_bRunning || m_furnaceFan)
            bOn = true;
    }

	return bOn;
}

int8_t HVAC::getMode()
{
	return m_EE.Mode;
}

void HVAC::setHeatMode(uint8_t mode)
{
	m_setHeat = mode % 3;
}

uint8_t HVAC::getHeatMode()
{
	return m_EE.heatMode;
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
	    m_idleTimer = m_EE.idleMin - 30;        // keep it from starting too quickly
}

void HVAC::enable()
{
	m_bEnabled = true;
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
	if(m_remoteTimer == 0)	// only get local temp if no remote
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
	if(m_outMax[0] != -50)      // preserve peaks longer
	{
		m_outMax[0] = m_outMax[1];
		m_outMin[0] = m_outMin[1];
	}
	else                        // initial value
	{
		m_outMin[0] = min;
		m_outMax[0] = max;
	}
	
	m_outMin[1] = min;
	m_outMax[1] = max;
}

void HVAC::resetFilter()
{
	m_EE.filterMinutes = 0;
	clearNotification(0);
}

bool HVAC::checkFilter(void)
{
	return (m_EE.filterMinutes >= 60*200);
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

void HVAC::setValues()
{
    sprintf(m_szSettings, "{\"m\":%d,\"am\":%d,\"hm\":%d,\"fm\":%d,\"ot\":%d,\"ht\":%d,\"c0\":%d,\"c1\":%d,\"h0\":%d,\"h1\":%d,\"im\":%d,\"cn\":%d,\"cx\":%d,\"ct\":%d,\"fd\":%d,\"ov\":%d}",
		m_EE.Mode,
		m_AutoMode,
		m_EE.heatMode,
		m_bFanMode,
		m_ovrTemp,
		m_EE.eHeatThresh,

    	m_EE.coolTemp[0], m_EE.coolTemp[1], m_EE.heatTemp[0], m_EE.heatTemp[1],
    	m_EE.idleMin, m_EE.cycleMin, m_EE.cycleMax, m_EE.cycleThresh, m_EE.fanPostDelay[digitalRead(P_REV)],
		m_EE.overrideTime);
}

void HVAC::pushData()
{
    char szData[300];

    sprintf(szData, "{\"r\":%d,\"fr\":%d,\"it\":%d,\"rh\":%d,\"tt\":%d,\"fm\":%d,\"ot\":%d,\"ol\":%d,\"oh\":%d,\"ct\":%d,\"ft\":%d,\"rt\":%d,\"td\":%d}",
                m_bRunning,
                getFanRunning(),
	        	m_inTemp, m_rh, m_targetTemp, m_EE.filterMinutes,
        		m_outTemp, m_outMin[1], m_outMax[1], m_cycleTimer, m_fanOnTimer, m_runTotal, m_tempDiffTotal);

    Particle.publish("hvacData", szData, 60, PRIVATE);
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
	"overridetime",
	"notify",
	"remotetemp",
	"remotetime",
	"pubtime",
	NULL
};

// Spark cloud vars (warning: returns button # to refresh)
// Todo: Change to return useful value, and refresh to &button
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
			m_EE.fanPostDelay[digitalRead(P_REV)] = constrain(val, 0, 60*5); // Limit 0 to 5 minutes
			break;
		case 6:     // cyclenmin
			m_EE.cycleMin = constrain(val, 60, 60*20); // Limit 1 to 20 minutes
			break;
		case 7:     // cyclemax
			m_EE.cycleMax = constrain(val, 60*2, 60*60); // Limit 2 to 60 minutes
			break;
		case 8:     // idlemin
			m_EE.idleMin = constrain(val, 60, 60*30); // Limit 1 to 30 minutes
			break;
		case 9:    // cyclethresh
			m_EE.cycleThresh = constrain(val, 5, 40); // Limit 0.5 to 4.0 degrees
			break;
		case 10:    // cooltempl
			setTemp(Mode_Cool, val, 0);
			m_bRecheck = true; // faster update
			return 1;
		case 11:    // cooltemph
			setTemp(Mode_Cool, val, 1);
			m_bRecheck = true;
			return 0;
		case 12:    // heattempl
			setTemp(Mode_Heat, val, 0);
			m_bRecheck = true;
			return 3;
		case 13:    // heattemph
			setTemp(Mode_Heat, val, 1);
			m_bRecheck = true;
			return 2;
		case 14:    // eheatthresh
			m_EE.eHeatThresh = constrain(val, 5, 50); // Limit 5 to 50 degrees F
			break;
		case 15:    // override
			if(val <= 0)    // cancel
			{
    		    m_ovrTemp = 0;
				m_overrideTimer = 0;
				m_bRecheck = true;
			}
			else
			{
				m_ovrTemp = constrain(val, -80, 80); // Limit to -8.0 to +8.0 degrees F
				m_overrideTimer = m_EE.overrideTime;
				m_bRecheck = true;
			}
			break;
		case 16:    // overridetime
			m_EE.overrideTime = constrain(val, 60*1, 60*60*5); // Limit 1 min to 5 hours
			break;
		case 17:    // notify
			{
				static char szNote[16];
				sVal.toCharArray( szNote, 16);
				addNotification(szNote);
			}
			return 11;
		case 18: // remotetemp
            if(val > 0)
            {
                m_inTemp = constrain(val, 650, 880); // Limit 65 to 88 degrees F
                m_remoteTimer = m_remoteTimeout;    // heartbeat
            }
            else
            {
                m_remoteTimer = 0;  // temp = 0 to cancel
            }
		    break;
		case 19: // remotetime
            m_remoteTimeout = constrain(val, 1, 60*5); // Limit 1 sec to 5 minutes
		    break;
        case 20: // pubtime
            m_pubTime = val;
            m_pubTimer = 2;
		    break;
	}
	return -1;      // default no button to refresh
}

bool HVAC::addNotification(const char *pszText)
{
	int i;

// Todo: Check for duplicate
	
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
