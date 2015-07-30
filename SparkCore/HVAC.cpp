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

//#define SIM

HVAC::HVAC()
{
	m_EE.fanPostDelay = 90; 	// 90 seconds after compressor stops
	m_EE.filterHours = 0;
	m_EE.cycleMin = 60;		    // 60 seconds minimum for a cycle
	m_EE.cycleMax = 60*15;		// 15 minutes maximun for a cycle
	m_EE.idleMin = 60*5;		// 5 minutes minimum between cycles
	m_EE.cycleThresh = 15;		// 1.5 degree cycle range
	m_EE.coolTemp[1] = 820;		// 81.0 default temps
	m_EE.coolTemp[0] = 790;		// 79.0
	m_EE.heatTemp[1] = 720;		// 74.0
	m_EE.heatTemp[0] = 680;		// 70.0
	m_EE.eHeatThresh = 30;		// Setting this low (30 deg) for now
	m_EE.overrideTime = 10 * 60;	// setting for override
	m_EE.id = 0xAA55;		    // EE value for validity check or struct size changes

	memset(m_fcData, -1, sizeof(m_fcData)); // invalidate forecast

//	m_fcPeaks[0].h = -50		// set as invalid

	m_outMax[0] = -50;		// set as invalid
	m_outMax[1] = -50;		// set as invalid

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

	if(m_overrideTimer)
	{
		m_overrideTimer--;
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

	int8_t h = (m_EE.heatMode == 2) ? m_AutoHeat : m_EE.heatMode;

	if(m_bStart && !m_bRunning)	            // Start signal occurred
	{
		int8_t mode = (m_EE.Mode == Mode_Auto) ? m_AutoMode : m_EE.Mode;
		m_bStart = false;

		switch(mode)
		{
			case Mode_Cool:
				fanSwitch(true);
				if(digitalRead(P_REV) != HIGH)
    				{
					digitalWrite(P_REV, HIGH);  // set to heatpump to cool (if heats, reverse this)
					delay(5000);                //    if no heatpump, remove
				}
				digitalWrite(P_COOL, HIGH);
				break;
			case Mode_Heat:
				if(h)  // gas
				{
					fanSwitch(false);
					digitalWrite(P_HEAT, HIGH);
				}
				else
				{
					fanSwitch(true);
					if(digitalRead(P_REV) != LOW)  // set to heatpump to heat (if heats, reverse this)
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
		Serial.print("Op started. inTemp=");
		Serial.println(m_inTemp);
	}

	if(m_bStop && m_bRunning)	        		// Stop signal occurred
	{
		m_bStop = false;
		digitalWrite(P_COOL, LOW);
		digitalWrite(P_HEAT, LOW);

		if(m_bFanRunning && m_bFanMode == false) // Note: furance manages fan
		{
			if(m_EE.fanPostDelay)			    // leave fan running to circulate air longer
				m_fanPostTimer = m_EE.fanPostDelay;
			else
				fanSwitch(false);
		}
		
		if(h)                  // count run time as fan time in winter
		{
			m_fanOnTimer += 60;         // furnace post fan is 60 seconds
			fanTimeAccum();
		}
        
		m_bRunning = false;
		m_idleTimer = 0;
		analyze();
	}

#ifdef SIM
		simulator();
#endif
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
				{
					Serial.print("Stop ");
					Serial.print(m_inTemp);
					Serial.print(" >= ");
					Serial.print(m_targetTemp);
					m_bStop = true;
				}
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
				Serial.print("Auto cool ");
				Serial.print(m_inTemp);
				Serial.print(" ");
				Serial.println(m_EE.coolTemp[0]);
				calcTargetTemp(Mode_Cool);
				m_AutoMode = Mode_Cool;
				bRet = true;
			}
			else if(m_inTemp <= m_EE.heatTemp[1] - m_EE.cycleThresh)
			{
				Serial.println("Auto heat");
				m_AutoMode = Mode_Heat;
				calcTargetTemp(Mode_Heat);
				if(m_EE.heatMode == 2)
				{
					if(m_inTemp < m_outTemp - (m_EE.eHeatThresh * 10)) 	// Use gas when efficiency too low for pump
						m_AutoHeat = 1;
					else
						m_AutoHeat = 0;
				}
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
		else	// Heating is required at around 25 under indoor    (testing)
		{	// Note: If outdoor temp climbs from 30 to 50, indoor temp doesn't drop from 70.
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
	if(digitalRead(P_REV) != (mode == Mode_Cool) )  // set heatpump same as current mode (Note: some models may be reversed)
		digitalWrite(P_REV, (mode == Mode_Cool) ? HIGH : LOW);

	if(m_overrideTimer)     // don't adjust during overide
		return;

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
				Serial.println("Error: outTemp range low");
				m_targetTemp = m_EE.coolTemp[0];
			}
			else if( m_outTemp > H )
			{
				Serial.println("Error: outTemp range high");
				m_targetTemp = m_EE.coolTemp[1];
			}else
			{
				m_targetTemp  = (m_outTemp-L) * (m_EE.coolTemp[1]-m_EE.coolTemp[0]) / (H-L) + m_EE.coolTemp[0];
			}
			break;
		case Mode_Heat:
			if( m_outTemp < L )
			{
				Serial.println("Error: outTemp range low");
				m_targetTemp = m_EE.heatTemp[0];
			}
			else if( m_outTemp > H )
			{
				Serial.println("Error: outTemp range high");
				m_targetTemp = m_EE.heatTemp[1];
			}else
			{
				m_targetTemp  = (m_outTemp-L) * (m_EE.heatTemp[1]-m_EE.heatTemp[0]) / (H-L) + m_EE.heatTemp[0];
			}
			break;
	}
	
	Serial.print(" target=");
	Serial.println(m_targetTemp);
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
	        m_idleTimer = 0;        // keep it from starting too quickly
}

void HVAC::enable()
{
	m_idleTimer = 10*60;            // start with a high idle, in case of power outage
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
#ifdef SIM
	if(m_inTemp != 0)	// get real temp once if simulating
		return;
#endif
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
		if(m_outMax[0] != m_outMax[1])
			m_outMax[0] = m_outMax[1];
		if(m_outMin[0] != m_outMin[1])
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
			r |= (m_EE.heatMode << 4);	    // 2
			r |= (m_bFanMode ? 0x40:0); 	// 1
			r |= (m_bRunning ? 0x80:0); 	// 1
			r |= (m_bFanRunning ? 0x100:0);  // 1
			r |= m_EE.eHeatThresh << 10;    // 6 (63 max)
			r |= m_EE.fanPostDelay << 16;	// 8 (255)
			r |= m_EE.cycleThresh << 24; 	// 8
			sprintf(m_szResult, "{\"c0\":%d,\"c1\":%d,\"h0\":%d,\"h1\":%d,\"it\":%d,\"tt\":%d,\"im\":%d,\"cn\":%d,\"cx\":%d,\"fh\":%d,\"ot\":%d,\"ol\":%d,\"oh\":%d,\"ct\":%d,\"ft\":%d,\"rt\":%d,\"td\":%d,\"ov\":%d,\"rh\":%d}",
			// 160-191
			m_EE.coolTemp[0], m_EE.coolTemp[1], m_EE.heatTemp[0], m_EE.heatTemp[1],
			m_inTemp, m_targetTemp, m_EE.idleMin, m_EE.cycleMin, m_EE.cycleMax, m_EE.filterHours,
			m_outTemp, m_outMin[1], m_outMax[1], m_cycleTimer, m_fanOnTimer, m_runTotal, m_tempDiffTotal, m_EE.overrideTime, m_rh);

			break;
		case 2: // temp
			r = m_inTemp;
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
	"overridetime",
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
			Serial.print("set cycleMax ");
			Serial.println(m_EE.cycleMax);
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
			m_overrideTimer = m_EE.overrideTime;
			break;
		case 16:    // overridetime
			m_EE.overrideTime = val;
			break;
		case 17:    // notify
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

#ifdef SIM
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
//		int Chg = 1;    // 0.1
//		if(Diff < 200)  // 20deg
//		Chg = -(int)(log(Diff * 0.01));

//		Serial.print("Sim:");

		switch(m_EE.Mode)
		{
			case Mode_Cool:
				m_inTemp--;
//				Serial.print("Cool");
				break;
			case Mode_Heat:
				m_inTemp++;
//				Serial.print("Heat");
				break;
			case Mode_Auto:
//				Serial.print("Auto ");
//				Serial.print(m_AutoMode);
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
//		int Chg = (int)(exp( Diff * 0.005 ));
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
#endif
