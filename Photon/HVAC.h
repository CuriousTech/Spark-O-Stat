//	HVAC Control
//
typedef struct
{
	int8_t h;   // hours ahead up to 72
	int8_t t;   // integer temperature value
} Forecast;

enum Mode
{
	Mode_Off,
	Mode_Cool,
	Mode_Heat,
	Mode_Auto
};

enum HeatMode
{
	Heat_HP,
	Heat_NG,
	Heat_Auto
};

typedef struct EEConfig_
{
	uint16_t coolTemp[2];	// cool to temp *10 low/high
	uint16_t heatTemp[2];	// heat to temp *10 low/high
	int16_t  cycleThresh;	// temp range for cycle *10
	uint8_t	 Mode;		// Off, Cool, Heat, Auto
	uint8_t  eHeatThresh;	// degree threshold to switch to gas
	uint16_t cycleMin;	// min time to run
	uint16_t cycleMax;	// max time to run
	uint16_t idleMin;	// min time to not run
	uint16_t filterMinutes;	// resettable minutes run timer (200 hours is standard change interval)
	uint16_t fanPostDelay[2];	// delay to run auto fan after [hp][cool] stops
	uint16_t overrideTime;	// time used for an override
	uint8_t  heatMode;	// heating mode (gas, electric)
	uint8_t  res;		//
	uint16_t id;
} EEConfig;

class HVAC
{
public:
	HVAC(void);
	void    disable(void);              // Shut it off
	void	service(void);			// call once per second
	uint8_t getState(void);       // return current run state
	bool    getFanRunning(void);    // return running
	int8_t  getMode(void);          // actual mode
	uint8_t getHeatMode(void);      // heat mode
	int8_t  getAutoMode(void);      // get current auto heat/cool mode
	int8_t  getSetMode(void);       // get last requested mode
	void	setMode(int8_t mode);	// request new mode; see enum Mode
	void    setHeatMode(uint8_t mode); // heat mode
	bool    getFan(void);           // is fan on
	void	setFan(bool on);		// auto/on mode
	void    filterInc(void);
	int16_t getSetTemp(int8_t mode, int8_t hl); // get temp set for a mode (cool/heat)
	void	setTemp(int8_t mode, int16_t Temp, int8_t hl); // set temp for a mode
	void	updateIndoorTemp(int16_t Temp, int16_t rh);
	void    updateOutdoorTemp(int16_t outTemp);
	void    updatePeaks(int8_t min, int8_t max);
	void	resetFilter(void);		// reset the filter hour count
	bool    checkFilter(void);
	void    resetTotal(void);
	int     setVar(String s);
	bool    addNotification(const char *pszText);
	void    clearNotification(int n);
	void    enable(void);

	EEConfig m_EE;
	char    m_szSettings[200];
	Forecast m_fcData[20];
	int16_t m_outTemp;          // adjusted current temp *10
	int16_t	m_inTemp;		    // current adjusted indoor temperature *10
	uint16_t m_targetTemp;      // end temp for cycle
	const char *m_pszNote[8];

private:
	void	fanSwitch(bool bOn);
	void	tempCheck(void);
	bool    preCalcCycle(int8_t mode);
	void    calcTargetTemp(int8_t mode);
	void	analyze(void);
	int     CmdIdx(String s, const char **pCmds);
    void    pushData(void);
    void    setValues(void);
    
	bool	m_bFanMode;		    // Auto=false, On=true
	bool	m_bFanRunning;		// when fan is running
	int8_t	m_AutoMode;		    // cool, heat
	int8_t	m_setMode;		    // new mode request
	int8_t	m_setHeat;		    // new heat mode request
	int8_t	m_AutoHeat;		    // auto heat mode choice
	bool	m_bRunning;	    	// is operating
	bool	m_bStart;		    // signal to start
	bool	m_bStop;	    	// signal to stop
	bool    m_bRecheck;
	bool    m_bEnabled;
	uint16_t m_runTotal;		// time HVAC has been running total since reset
	uint16_t m_tempDiffTotal;	// total temp change for total runs (6553.5 deg of change)
	uint16_t m_fanOnTimer;		// time fan is running
	uint16_t m_cycleTimer;		// time HVAC has been running
	uint16_t m_fanPostTimer;	// timer for delay
	uint16_t m_idleTimer;		// time not running
	int16_t m_overrideTimer;	// countdown for override in seconds
	int16_t m_rh;
	int16_t m_startingTemp;		// temp at start of cycle *10
	int16_t m_startingRh;		// rh at start of cycle *10
	int8_t  m_outMin[2], m_outMax[2];
	int8_t  m_ovrTemp;          // override delta of target
    uint16_t m_remoteTimeout;   // timeout for remote sensor
    uint16_t m_remoteTimer;     // in seconds
	int8_t	m_furnaceFan;	    // fake fan timer
	uint16_t m_pubTime;         // seconds between publishes
	uint16_t m_pubTimer;        // seconds until next publish
};

#define	P_FAN	D4
#define	P_COOL	D5
#define	P_REV D6
#define	P_HEAT	D7
