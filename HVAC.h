//	HVAC Control
//
typedef struct
{
	int8_t h;   // hours ahead up to 72
	int8_t t;   // integer temperature value
} Forecast;

typedef struct
{
	unsigned int time;  // end time
    uint16_t secs;       // seconds on
	uint16_t t1;         // start temp
	uint16_t t2;         // end temp
	uint16_t rh1;        // start rh
	uint16_t rh2;        // end rh
	uint8_t mode;       // mode
} Log;  // 16 bytes

enum Mode
{
	Mode_Off,
	Mode_Cool,
	Mode_Heat,
	Mode_Auto
};

typedef struct EEConfig_
{
	uint16_t coolTemp[2];	   	// cool to temp *10 low/high
	uint16_t heatTemp[2];	   	// heat to temp *10 low/high
	int16_t cycleThresh;		// temp range for cycle *10
	uint8_t	Mode;				// Off, Cool, Heat, Auto
    uint8_t  eHeatThresh;       // degree threshold to switch to gas
	uint16_t cycleMin;	    	// min time to run
	uint16_t cycleMax;	    	// max time to run
	uint16_t idleMin;			// min time to not run
	uint16_t filterHours;		// resettable hours run timer (200 hours is standard change interval)
	uint16_t fanPostDelay;    	// delay to run auto fan after heat/cool stops
	uint16_t id;
} EEConfig;

class HVAC
{
public:
	HVAC(void);
	void	service(void);			// call once per second
    bool    getRunning(void);       // return running
    int8_t  getMode(void);          // actual mode
    uint8_t  getHeatMode(void);      // heat mode
    int8_t  getAutoMode(void);      // get current auto heat/cool mode
    int8_t  getSetMode(void);       // get last requested mode
	void	setMode(int8_t mode);	// request new mode; see enum Mode
    void    setHeatMode(uint8_t mode); // heat mode
    bool    getFan(void);           // is fan on
	void	setFan(bool on);		// auto/on mode
    void    fanTimeAccum(void);
    int16_t getSetTemp(int8_t mode, int8_t hl); // get temp set for a mode (cool/heat)
	void	setTemp(int8_t mode, int16_t Temp, int8_t hl); // set temp for a mode
	void	updateIndoorTemp(int16_t Temp, int16_t rh);
    void    updateOutdoorTemp(int16_t outTemp);
    void    updatePeaks(void);
	void	resetFilter(void);		// reset the filter hour count
    bool    checkFilter(void);
    void    resetTotal(void);
	int		getVar(String s);
    int     setVar(String s);
    bool    addNotification(const char *pszText);
    void    clearNotification(int n);

    EEConfig m_EE;
    char    m_szResult[128];
    Forecast m_fcData[18];
    int16_t m_outTemp;          // adjusted current temp *10
    uint16_t m_targetTemp;      // end temp for cycle
    const char *m_pszNote[8];

private:
	void	fanSwitch(bool bOn);
	void	tempCheck(void);
    bool    preCalcCycle(int8_t mode);
    void    calcTargetTemp(int8_t mode);
    int     tween(uint16_t t1, uint16_t t2, int m, int8_t h);
    int     scaleRange(uint16_t inL, uint16_t inH, int8_t outL, int8_t outH, int16_t outTemp);
	void	analyze(void);
    int     CmdIdx(String s, const char **pCmds);
	void	simulator(void);

	bool	m_bFanMode;			// Auto=false, On=true
	int8_t	m_AutoMode;			// cool, heat
	int8_t	m_setMode;			// new mode request
	int8_t	m_heatMode;			// heating mode (gas, electric)
    int8_t  m_eHeatThresh;      // degree threshold to switch to gas
	bool	m_bRunning;			// is operating
	bool	m_bFanRunning;		// when fan is running
	bool	m_bStart;			// signal to start
	bool	m_bStop;			// signal to stop
	bool	m_bSim;				// simulating
	uint16_t m_runTotal;	    // time HVAC has been running total since reset
    uint16_t m_tempDiffTotal;   // total temp change for total runs (6553.5 deg of change)
	uint16_t m_fanOnTimer;		// time fan is running
	uint16_t m_cycleTimer;		// time HVAC has been running
	uint16_t m_fanPostTimer;	// timer for delay
	uint16_t m_idleTimer;		// time not running
	int16_t	m_inTemp;			// current indoor temperature *10
    int16_t m_rh;
	int16_t m_startingTemp;		// temp at start of cycle *10
	int16_t m_startingRh;		// rh at start of cycle *10
	Forecast m_fcPeaks[3];      // min/max
    Log     m_logs[32];         // 512 bytes
};

#define	P_FAN	D4
#define	P_COOL	D5
#define	P_REV	D6
#define	P_HEAT	D7
