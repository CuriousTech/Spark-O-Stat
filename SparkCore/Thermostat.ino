// This #include statement was automatically added by the Spark IDE.
#include "Adafruit_mfGFX/Adafruit_mfGFX.h"

// This #include statement was automatically added by the Spark IDE.
#include "Adafruit_ILI9341/Adafruit_ILI9341.h"

// This #include statement was automatically added by the Spark IDE.
#include "PietteTech_DHT/PietteTech_DHT.h"

//
#include "application.h"
//#include "TFT.h"
#include "UTouch.h"
#include "XMLReader.h"
#include "HVAC.h"
#include "Encoder.h"
 
#define FONT_SPACE	6
#define FONT_Y		8
#define CYAN		0xfe00
#define BRIGHT_RED	0xc03f
#define GRAY1		0x8410
#define GRAY2		0x4208

SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

uint8_t wifi_retry_count = 0;

//#define T_CAL     // switch to touchscreen calibration mode (draws hits and 4 temp buttons are calibration values adjusted by thumbwheel)

#define DHT_TEMP_ADJUST	(-3.0)	// Adjust indoor temp by degrees
#define DHT_RH_ADJUST    (3.0)	// Adjust indoor Rh by %
#define DHT_PERIOD  (10 * 1000)  // 10 seconds

#define ONE_DAY_MILLIS (24 * 60 * 60 * 1000)

unsigned long lastSync;

int8_t  tz = -5;         // timezone and DST
char    ZipCode[] = "41042";

//--
uint16_t blankSecs = 120;
int8_t  brightness = 60; // in %
int8_t  dimLevel = 2;    // dim level.  0 = off
unsigned long lBacklightTimer;
bool bScreenOn = true;

void drawButton(int8_t btn, bool bDown, bool bFast);
void screenOn(void);

Adafruit_ILI9341 tft = Adafruit_ILI9341(TX, A7, RX);

UTouch touch(A2, A0);
//--

HVAC hvac = HVAC();

char buffer[260]; // buffer for xml when in use

void Encoder_callback();

Encoder rot(D2, D3, Encoder_callback);

void Encoder_callback()
{
    rot.isr();
}

// declaration for DHT22 handler
void dht_wrapper(); // must be declared before the lib initialization

unsigned long lUpdateDHT;
bool bDHT;
// DHT instantiate
PietteTech_DHT DHT(A1, DHT22, dht_wrapper);

void dht_wrapper()
{
    DHT.isrCallback();
}

//-----

static const char _days_short[7][4] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

void displayTime()
{
	char szTime[16];
	static char lastMin = -1;

	sprintf(szTime, "%s %2d:%02d:%02d %cM", _days_short[Time.weekday()-1], Time.hourFormat12(), Time.minute(), Time.second(), Time.isAM() ? 'A':'P');
	tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	tft.setTextSize(2);

	if(lastMin == szTime[11]) // draw just seconds
	{
		szTime[12] = 0;     // cut to seconds
		tft.setCursor(38 + (10*FONT_SPACE), 0);
		tft.println(szTime + 10);
		return;
	}

	lastMin = szTime[11];
	if(szTime[4] == '0') szTime[4] = ' ';
	tft.setCursor(38, 0);
	tft.println(szTime); // Time.format("%a %I:%M:%S%p"));
}

//---------------------------

XML_tag_t tags[] =
{
	{"time-layout", "time-coordinate", "local", 19},
	{"temperature", "type", "hourly", 19},
	{NULL}
};

unsigned long lUpdateFcst;
unsigned long lUpdateOutTemp;
unsigned long FcstInterval;
bool bReading;
bool bUpdateDST;

void xml_callback(int8_t item, int8_t idx, char *p)
{
	int8_t newtz;
	int8_t h;
	int8_t d;
	static int8_t hO;
	static int8_t lastd;

	switch(item)
	{
		case 0:                             // valid time
			if(!idx)                        // first item isn't really data.  Use for past data
			{
				hO = 0;                     // reset hour offset
				lastd = Time.day();
				break;
			}
			d = atoi(p + 8);                // 2014-mm-ddThh:00:00-tz:00
			h = atoi(p + 11);
			if(d != lastd) hO += 24;        // change to hours offset
			lastd = d;
			hvac.m_fcData[idx-1].h = h + hO;

			newtz = atoi(p + 20);
			if(p[19] == '-')
				newtz = -newtz;

			if(idx < 3 && newtz != tz)
			{
				tz = newtz;
				if(idx == 1)
				{
					Time.zone(tz);      // needs change now
				}
				else
				{
					bUpdateDST = true;  // change it at 2AM
				}
			}
			break;
		case 1:                             // temperature
			if(!idx)                        // 1st value is not temp
				break;
			hvac.m_fcData[idx-1].t = atoi(p);
			break;
	}
}

XMLReader xml(buffer, 257, xml_callback);

// Setup:  NDFD Data For Single Point, zipcode, hourly temp only  (for rh add &rh=rh, for alerts add &wwa=wwa, etc.)
//  http://graphical.weather.gov/xml/sample_products/browser_interface/ndfdXML.htm
//  Other options would be to request different data at different times to split up data that's too large for one time
void GetForecast()
{
	lUpdateFcst = millis();
	
	Serial.print("Updating forecast at ");
	Serial.println(Time.timeStr());
	
	char *p_cstr_array[] =
	{
		"/xml/sample_products/browser_interface/ndfdXMLclient.php?zipCodeList=",
		ZipCode,
		"&Unit=e&temp=temp&Submit=Submit",
		NULL
	};

	bReading = xml.begin("graphical.weather.gov", p_cstr_array);
	if(!bReading)
	{
		Particle.publish("status", "forecast failed");
		hvac.addNotification("Network Error");
	}
}

// get value at current minute between hours
int tween(int8_t t1, int8_t t2, int m, int8_t h)
{
	if(h == 0) h = 1; // div by zero check
	double t = (double)(t2 - t1) * (m * 100 / (60 * h)) / 100;
	return (int)((t + t1) * 10);
}

// This is where the "current" outdoor temperature is calculated
// When starting up, it may be up to 2 hours ahead
void displayOutTemp()
{
    lUpdateOutTemp = millis();

    int8_t hd = Time.hour() - hvac.m_fcData[0].h;       // hours past 1st value
    int16_t outTemp;

    if(hd < 0)                                          // 1st value is top of next hour
    {
//        Serial.println("future");
        outTemp = hvac.m_fcData[0].t * 10;              // just use it
    }
    else
    {
        int m = Time.minute();              // offset = hours past + minutes of hour

        if(hd) m += (hd * 60);              // add hours ahead (up to 2)

        outTemp = tween(hvac.m_fcData[0].t, hvac.m_fcData[1].t, m, hvac.m_fcData[1].h - hvac.m_fcData[0].h);
    }

    hvac.updateOutdoorTemp(outTemp);

    drawButton(5, false, true);
}

#define Fc_Left     24
#define Fc_Top      26
#define Fc_Right    200
#define Fc_Bottom   77

void drawForecast()
{
	int8_t min = 126;
	int8_t max = -50;
	int8_t i;

	if(hvac.m_fcData[0].h == -1)          // no first run
		return;
		
	int8_t hrs = ( ((hvac.m_fcData[0].h - Time.hour()) + 1) % 3 ) + 1;   // Set interval to 2, 5, 8, 11..
	int8_t mins = (60 - Time.minute() + 54) % 60;   // mins to :54, retry will be :59

	if(mins > 10 && hrs > 2) hrs--;     // wrong

	FcstInterval = ((hrs * 60) + mins) * 60;
    // Get min/max
	for(i = 0; i < 18; i++)
	{
		int8_t t = hvac.m_fcData[i].t;
		if(min > t) min = t;
		if(max < t) max = t;
    }

	if(min == max) max++;   // div by 0 check
	
	hvac.updatePeaks(min, max);

	int16_t y = Fc_Top;
	int16_t incy = (Fc_Bottom - Fc_Top) / 3;
	int16_t dec = (max - min)/3;
	int16_t t = max;
	int16_t x;

	tft.setTextSize(1);
	tft.setTextColor( ILI9341_YELLOW, ILI9341_BLACK );

	tft.fillRect(0, Fc_Top, Fc_Right, Fc_Bottom-Fc_Top+FONT_Y+1, ILI9341_BLACK);

    // temp scale and h-lines
	for(i = 0; i <= 3; i++)
	{
		x = Fc_Left - FONT_SPACE - 1;           // to left
		if( t < 0) x -= FONT_SPACE;             // neg
		if( t < -9 || t > 9) x -= FONT_SPACE;   // 2 digit
		if( t > 99) x -= FONT_SPACE;            // another digit (probably not -100)

        tft.setCursor(x, y-(FONT_Y>>1));
        tft.print(t);
		tft.drawFastHLine(Fc_Left, y, Fc_Right-Fc_Left, GRAY1);
		y += incy;
		t -= dec;
	}

	tft.drawFastVLine(Fc_Left, Fc_Top, Fc_Bottom - Fc_Top, GRAY1); // left side
	tft.drawFastVLine(Fc_Right, Fc_Top, Fc_Bottom - Fc_Top, GRAY1); // right side

	int8_t day = Time.weekday()-1;              // current day
	int8_t h0 = Time.hour();                    // zeroeth hour
	int8_t pts = hvac.m_fcData[17].h - h0;
	int8_t h;
	int16_t day_x = 0;

	if(pts <= 0) return;                     // error

	for(i = 0, h = h0; i < pts; i++, h++)    // v-lines
	{
		x = Fc_Left + (Fc_Right-Fc_Left) * i / pts;
		if( (h % 24) == 0) // midnight
		{
			tft.drawFastVLine(x, Fc_Top, Fc_Bottom - Fc_Top, GRAY1);
			if(x - 49 > Fc_Left) // fix 1st day too far left
			{
				tft.setCursor(day_x = x - 49, Fc_Bottom + 2);
				tft.print(_days_short[day]);
			}
			if(++day >6) day = 0;
		}
		if( (h % 24) == 12) // noon
		{
			tft.drawFastVLine(x, Fc_Top, Fc_Bottom - Fc_Top, GRAY2);
		}
	}

	day_x += 84;
	if(day_x < Fc_Right - (FONT_SPACE*3) )
	{
		tft.setCursor(day_x, Fc_Bottom + 2);       // draw last day
		tft.print(_days_short[day]);
	}
	int16_t y2 = Fc_Bottom - 1 - (hvac.m_fcData[0].t - min) * (Fc_Bottom-Fc_Top-2) / (max-min);
	int16_t x2 = Fc_Left;

	for(i = 0; i < 18; i++)
	{
		int y1 = Fc_Bottom - 1 - (hvac.m_fcData[i].t - min) * (Fc_Bottom-Fc_Top-2) / (max-min);
		int x1 = Fc_Left + (hvac.m_fcData[i].h - h0) * (Fc_Right-Fc_Left) / pts;
		tft.drawLine(x2, y2, x1, y1, ILI9341_RED);
		x2 = x1;
		y2 = y1;
	}

	displayOutTemp();
}

//----------------
typedef struct
{
	const uint8_t x, y, w, h;
} Button;


Button buttons[] =
{// Left, top, wid, hi
    {224, 140,  90, 24},    // 0 cool temp high (do not change positon: buttons 0-3 refrenced elsewhere)
    {224, 164,  90, 24},    // 1 cool temp low
    {224, 188,  90, 24},    // 2 heat temp high
    {224, 212,  90, 24},    // 3 heat temp low

    {224,  112,  90, 24},    // 4 target temp

    {240,  22,  80, 24},    // 5 out temp - change button # in displayOutTemp
    {240,  46,  80, 24},    // 6 in temp  - change button #s in updateDHT
    {240,  70,  80, 24},    // 7 in rh    - "  "

    {  4,  91,  72, 34},    // 8 fan  (do not change positon: fan/mode reference in hvac.setMode)
    {  4, 128,  72, 34},    // 9 mode
    {  4, 165,  72, 34},    // 10 gas/elec

    {  4, 202, 216, 34},    // 11 Notifications (filter)  18 char max

    { 0, 0, 0, 0}
};

int8_t  adjustMode;
int8_t  dispNote = -1;

const char  *pszHvacFan[] = {"Auto", " On "};
const char  *pszHvacMode[] = {"Off ", "Cool", "Heat", "Auto"};
const char  *pszHeatMode[] = {" HP ", " NG ", "Auto"};

// convert n*10 to XXX.X with neg
void cvtNum(char *p, int num)
{
    if( num < 0)
    {
        p[0] = '-';
        num = -num;
    }
    else
    {
        p[0] = ' ';
    }
    p[4] = '0' + (num % 10);
    num /= 10;
    p[3] = '.';
    p[2] = '0' + (num % 10);
    num /= 10;
    p[1] = num ? ('0' + (num % 10)) : p[0]; // copy space or - if 2.1 digits
    num /= 10;
    if(p[1] == '-') p[0] = ' '; // remove --1.1 if -1.1
    if(num) p[1] = '0' + num;   // 3.1 will only be +
    p[5] = 0;
}

#ifdef T_CAL
// convert n to XXXXX with neg
void cvtNumD(char *p, int num)
{
    if( num < 0)
    {
        p[0] = '-';
        num = -num;
    }
    else
    {
        p[0] = ' ';
    }
    p[4] = '0' + (num % 10);
    num /= 10;
    p[3] = '0' + (num % 10);
    num /= 10;
    p[2] = '0' + (num % 10);
    num /= 10;
    p[1] = num ? ('0' + (num % 10)) : p[0];
    num /= 10;
    if(num) p[1] = '0' + num;
    p[5] = 0;
}
#endif

int16_t vals[5]; // the displayed value of a temp button

void drawButton(int8_t btn, bool bDown, bool bFast) // draw a button with pressed state and updated text
{
    uint16_t fg = ILI9341_WHITE;                // default text color
    uint16_t bg = bDown ? ILI9341_RED:ILI9341_BLACK;    // default bg color
    bool    bDeg = false;

    char szText[8] = "";
    char *pszText = szText;

    if(btn < 0)
        return;

    int16_t v;
    int i;
    static bool bBlink = false;
    int8_t m = (hvac.getMode() == Mode_Auto) ? hvac.getAutoMode() : hvac.getMode();

    switch(btn)
    {
        case 0: // cool temp hi
        case 1: // cool temp low
        case 2: // heat temp hi
        case 3: // heat temp low
#ifdef T_CAL
            v = touch.getCal(btn);
            if(bFast && v == vals[btn])
                return;
            cvtNumD(szText, vals[btn] = v);
            bg = (adjustMode == btn) ? GRAY1 : ILI9341_BLACK;
#else       //
            v = hvac.getSetTemp( (btn < 2 ) ? Mode_Cool : Mode_Heat, (btn^1) & 1 );
            if(bFast && v == vals[btn])
                return;
            cvtNum(szText, vals[btn] = v );
            fg = (btn < 2) ? ILI9341_BLUE:ILI9341_RED;
            bg = (adjustMode == btn) ? GRAY1 : ILI9341_BLACK;
            bDeg = true;
#endif
            break;
        case 4:     // target temp
            v = hvac.m_targetTemp;
            if(bFast && v == vals[btn])
                return;
            cvtNum(szText, vals[btn] = v );
            fg = ILI9341_GREEN;
            switch(m)
            {
                case Mode_Cool:
                    fg = ILI9341_BLUE;
                    break;
                case Mode_Heat:
                    fg = ILI9341_RED;
                    break;
            }
            bBlink = !bBlink;
            bg = hvac.getState() ? (bBlink ? GRAY1:fg) : ILI9341_BLACK;
            bDeg = true;
            break;

        case 5: // out temp
            cvtNum(szText, hvac.m_outTemp );
            bDeg = true;
            break;
        case 6: // in temp
            cvtNum(szText, (int)((DHT.getFahrenheit() + DHT_TEMP_ADJUST) * 10) );         // display adjusted indoor temp
            bDeg = true;
            break;
        case 7: // in rh
            cvtNum(szText, (int)((DHT.getHumidity() + DHT_RH_ADJUST) * 10) );
            szText[5] = '%';
            szText[6] = 0;
            break;
        case 8: // fan
            pszText = (char *)pszHvacFan[ hvac.getFan() ];
            bg = bDown ? ILI9341_RED:ILI9341_BLUE;
            break;
        case 9: // mode
            pszText = (char *)pszHvacMode[ hvac.getSetMode() ];
            bg = bDown ? ILI9341_RED:ILI9341_BLUE;
            break;
        case 10: // heat mode
            pszText = (char *)pszHeatMode[ hvac.getHeatMode() ];
            bg = bDown ? ILI9341_RED:ILI9341_BLUE;
            break;
        case 11: // Notifications
            pszText = "               ";
            for(i = 0; i < 8; i++)
            {
                if(hvac.m_pszNote[i])   // find one
                {
                    pszText = (char *)hvac.m_pszNote[i];
                    dispNote = i;
                    tone(D1, 3000, 200);    // make noise
                    delay(200);
                    tone(D1, 3200, 200);
                    bg = ILI9341_BLUE;
                    break;
                }
            }
            break;
    }

    // erase the button area
//    tft.fillRect(buttons[btn].x, buttons[btn].y, buttons[btn].w, buttons[btn].h, ILI9341_BLACK);
    tft.drawRoundRect(buttons[btn].x, buttons[btn].y, buttons[btn].w, buttons[btn].h, 3, bg);

    int16_t l = (buttons[btn].w>>1) - (strlen(pszText) * FONT_SPACE);
    int16_t t = (buttons[btn].h>>1) + 1 - FONT_Y;

    tft.setTextSize(2);
    tft.setCursor(buttons[btn].x + l, buttons[btn].y + t);
    tft.setTextColor(fg, ILI9341_BLACK);
    tft.print((char*)pszText);

    if(bDeg)
        tft.drawCircle(buttons[btn].x + l + (FONT_SPACE*10)+4, buttons[btn].y + t + 3, 2, fg);     // add a degree symbol
}

void refreshTemps(void)
{
    for(int i = 0; i < 4; i++)
        drawButton(i, false, true);

    drawButton(11, false, true);
}

void Backlight(uint8_t bright)
{
	if(bright == 0) //off
	{
		analogWrite(A6, 0);
	}
	else if(bright >= 100) //100%
	{
		analogWrite(A6, 255);
	}
	else //1...99%
	{
		analogWrite(A6, (uint16_t)bright*255/100);
	}
}

void HandleTouch(bool bDown)
{
	static int8_t lastButton = -1;
	static int16_t lastX = -1, lastY = -1;
	static bool bPress = false;
	
	lBacklightTimer = millis();     // reset screensaver
	
	if(!bScreenOn)
	{
		Backlight(brightness);
		bScreenOn = true;
		lastButton = -1;
		return;     // block first press if screen was off
	}
	
	if( !bDown )      // button release
	{
		drawButton(lastButton, false, false);
		bPress = false;
		return;
	}
	
	touch.read();
	int16_t x = touch.getX();
	int16_t y = touch.getY();

#ifdef T_CAL // display for calibration
        Serial.print("Touch TP_X=");
        Serial.print(touch.TP_X);
        Serial.print(" TP_Y=");
        Serial.print(touch.TP_Y);
        Serial.print(" X=");
        Serial.print(touch.getX());
        Serial.print(" Y=");
        Serial.println(touch.getY());

        if(lastX != -1)
        {
		    tft.drawFastHLine(lastX-20, lastY, 40, ILI9341_BLACK);
		    tft.drawFastVLine(lastX, lastY-20, 40, ILI9341_BLACK);
		    tft.drawCircle(lastX, lastY, 15, ILI9341_BLACK);
        }
        tft.drawFastHLine(x-20, y, 40, ILI9341_CYAN);
        tft.drawFastVLine(x, y-20, 40, ILI9341_CYAN);
    	tft.drawCircle(x, y, 15, ILI9341_CYAN);
#else
    if(bPress)     // Don't handle swipes
        return;
#endif

    lastButton = -1;
    lastX = x;
    lastY = y;

    int i;

    for(i = 0; buttons[i].w; i++) // determine if press is on a button
    {
        if(x >= buttons[i].x && x <= (buttons[i].x+buttons[i].w) && y >= buttons[i].y && y <= (buttons[i].y+buttons[i].h) )
        {
            lastButton = i;
        }
    }

    bPress = true;

    if(lastButton < 0) // nothing else to do
        return;

    switch(lastButton)
    {
        case 0: // cooltemp hi
        case 1: // cooltemp low
        case 2: // heattemp hi
        case 3: // heattemp low
            i = adjustMode;
            adjustMode = lastButton;
            drawButton(i, false, false);  // remove prev selection
            break;
        case 8: // Fan auto, on
            hvac.setFan( !hvac.getFan() );
            break;
        case 9: // off, Cool, heat, auto
            hvac.setMode( (hvac.getSetMode() + 1) & 3 );
            drawButton(1, false, false);  // remove prev highlight and display current highlight
            drawButton(2, false, false);
            break;
        case 10: // heat mode
            hvac.setHeatMode( (hvac.getHeatMode() + 1) % 3 );
            drawButton(9, false, false);
            break;
        case 11: // Notifications
            if(dispNote >= 0)
            {
                hvac.clearNotification(dispNote);
            }
            break;
    }

    tone(D1, 8000, 30);    // touch feedback
    drawButton(lastButton, true, false);  // pressed
}

bool EncoderCheck()
{
    int r = rot.read();

    if(r == 0)
        return false;

    screenOn();

#ifdef T_CAL
    int16_t t = touch.getCal(adjustMode);

    if(r > 0) t += 10;
    else t -= 10;
    tone(D1, 7000, 30);

    touch.setCal(adjustMode, t);
#else //
    int8_t m = (adjustMode < 2) ? Mode_Cool : Mode_Heat;

    int16_t t = hvac.getSetTemp(m, (adjustMode ^ 1) & 1 );

    if(r > 0) t++;
    else    t--;
    tone(D1, 7000, 30);

    hvac.setTemp(m, t, (adjustMode ^ 1) & 1);
#endif
    refreshTemps();
    return true;
}

void DrawButtons()
{
    const char szLabels[3][4] = {"Out", "In", "Rh"};
    const char szLabels2[3][6] = {"Fan", "Mode", "Heat"};
    int8_t i;

    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setTextSize(1);
    for(i = 0; i < 3; i++)
    {
        tft.setCursor(buttons[i+5].x - (FONT_SPACE*3), buttons[i+5].y + 4);
    	tft.print((char *)szLabels[i]);
    }

    tft.setTextSize(2);
    for(i = 0; i < 3; i++)
    {
        tft.setCursor(buttons[i+8].x + buttons[i+8].w + 10, buttons[i+8].y + 8);
    	tft.print((char *)szLabels2[i]);
    }

    for(i = 0; buttons[i].w; i++)
    {
        drawButton(i, false, false);
    }
}

bool displayPinState(int n, int x)
{
    static int8_t pins[] = {P_FAN, P_COOL, P_REV, P_HEAT};
    const char *s[] = {"F", "C", "R", "H"};
    static int8_t old[] = {-1,-1,-1,-1};

    tft.setTextSize(1);

    if(digitalRead(pins[n]) != old[n])
    {
        old[n] = digitalRead(pins[n]);
        tft.setCursor(x, 0);
        tft.setTextColor(old[n] ? ILI9341_GREEN : ILI9341_WHITE, old[n] ? ILI9341_RED : ILI9341_BLACK);
    	tft.println((char *)s[n]);
        Serial.print("PIN ");
        Serial.print(s[n]);
        Serial.print(" ");
        Serial.println(old[n]);
        return true;
    }
    return false;
}

// debug helper
void displayHvacPins()
{
    for(int i = 0, x = 320-(FONT_SPACE*4); i < 4; i++)
    {
        displayPinState(i, x);
        x += FONT_SPACE;
    }

    if(hvac.checkFilter())
    {
        hvac.addNotification("Change Filter");
        drawButton(11, false, false);
    }
}

void screenOn()
{
    lBacklightTimer = millis();     // reset screensaver

    if(!bScreenOn)
    {
        Backlight(brightness);
        bScreenOn = true;
    }
}

int sf_setVar(String s)
{
    screenOn();
    int r = hvac.setVar(s);

    if(r < 0) return 0;

    if(r <= 5)          // set temps
    {
        refreshTemps(); // all can change
        return 0;
    }
    drawButton(r, false, false);
    if(r == 9)         // mode
        refreshTemps();
    return 0;
}

int sf_getVar(String s)
{
    return hvac.getVar(s);
}

void UpdateEE()  // check for any changes that haven't been saved
{
    EEPROM.put(0, hvac.m_EE);
}

void ReadEE()   // read EEPROM on startup
{
    if( EEPROM.read( sizeof(EEConfig)-1 ) != 0xAA) // uninitialized
        return;

    EEPROM.get(0, hvac.m_EE);
    hvac.setMode(hvac.getMode()); // set request mode to EE mode
    hvac.setHeatMode(hvac.getHeatMode());
}

// signalStrength should be mapped into 0..+127
void drawSignal(int x, int y, int wh)
{
	int signalStrength = -WiFi.RSSI();
    int dist = wh  / 10; // distance between blocks
    int sect = 127 / 10; //

    tft.drawRoundRect(x, y, wh, wh, 3, ILI9341_BLUE);

    x += dist>>1;                    // set starting point for first line
    y += wh - (dist>>1);
    
    for (int i = 0; i < 10; i++)
    {
        int h = (i+1)*dist;
        if (signalStrength > i * sect)    // draw bar if signal strength is above threshold
            tft.drawLine(x + i*dist, y, x + i*dist, y - (i+1)*dist, ILI9341_GREEN);
        else // otherwise don't draw and bail out
            tft.drawLine(x + i*dist, y, x + i*dist, y - (i+1)*dist, GRAY2);
    }
}

//------------------------------
void setup()
{
	Particle.function("setvar", sf_setVar);
	Particle.function("getvar", sf_getVar);
	Particle.variable("result", hvac.m_szResult, STRING);
	Serial.begin(9600);
	
	ReadEE();
	
	tft.begin();
	tft.setRotation(3);
	tft.fillScreen(ILI9341_BLACK);
	
	tft.setFont(CENTURY_8);

	pinMode(A6, OUTPUT);
	Backlight(brightness);

	//                h,   w,  top, btm, lft, rght, 
	touch.InitTouch(240, 320, 2000, 210, 250, 1860, LANDSCAPE);
	
	lUpdateDHT = millis();
	Time.zone(tz);
	
	lastSync = millis();
	lUpdateFcst = millis();
	lBacklightTimer = millis();
	
	FcstInterval = 40; // first time = 30 seconds
	DrawButtons();

	DHT.acquire();
	bDHT = true;
	hvac.enable();
	WiFi.on();
}

void loop()
{
    static int8_t lastSec;
    static int8_t lastHour;
    static int8_t lastMode;
    static bool bDown = false;
    static uint8_t nState = 0;
    static bool bFan = false;
    char szPub[128];

    if( touch.pressed() )
    {
        bDown = true;
        HandleTouch(true);
    }
    else
    {
        if(bDown)
            HandleTouch(false);     // release
        bDown = false;
    }
    
    while( EncoderCheck() );

	if(Time.second() != lastSec)   // things to do every second
	{
		lastSec = Time.second();

        if(lastSec & 1)
        {
            if(wifi_retry_count < 10)
            {
                if(!WiFi.ready())
                {
                    WiFi.connect();
                    wifi_retry_count++;
                }
                else if(!Particle.connected())
                {
                    Particle.connect();
                    wifi_retry_count++;
                }
            }
            else
            {
                WiFi.off();
                wifi_retry_count = 0;
                WiFi.on();
            }
        }

        if(millis() - lastSync >= ONE_DAY_MILLIS)
        {
            Particle.syncTime();       // time sync every 24 hours
            lastSync = millis();
        }

        if(lastHour != Time.hour())     // things to do on the hour
        {
            lastHour = Time.hour();

            if(lastHour == 2 && bUpdateDST) // DST changes at 2AM
            {
                bUpdateDST = false;
                Time.zone(tz);
            }

            if(dispNote >= 0)  // if notification or critical error
            {
                tone(D1, 3000, 200);    // make noise
                delay(200);
                tone(D1, 3200, 200);
            }
	    }

        displayTime();
        hvac.service();
        drawSignal(150, 90, 40);

        if(hvac.getMode() != lastMode || hvac.getState() != nState || bFan != hvac.getFanRunning())   // erase prev highlight
        {
            lastMode = hvac.getMode();
            drawButton(1, false, false);
            drawButton(2, false, false);
            sprintf(szPub, "{\"Mode\": %u, \"State\": %u, \"Fan\": %u}", lastMode, nState = hvac.getState(), bFan = hvac.getFanRunning());
            Particle.publish("stateChg", szPub);
        }

        drawButton(4, false, false);     // target

        displayHvacPins();

        if(bDHT)    // this is 1 sec after acquire
        {
            bDHT = false;
            if(DHT.getStatus()  == DHTLIB_OK)
            {
                hvac.updateIndoorTemp( (int)((DHT.getFahrenheit() + DHT_TEMP_ADJUST) * 10), (int)((DHT.getHumidity()+DHT_RH_ADJUST) * 10) );

//                Serial.print("Temp: ");
//                Serial.println( DHT.getFahrenheit() );
                drawButton(6, false, true);     // in temp
                drawButton(7, false, true);     // rh
            }

            lUpdateDHT = millis();
        }
        else if(millis() - lUpdateDHT >= DHT_PERIOD)
        {
		    DHT.acquire();
		    bDHT = true;
        }

        if(bReading)
        {
            bReading = xml.service(tags);
            if(!bReading)
            {
//              Serial.print("XMLReader done: Status = ");
//              Serial.println(xml.getStatus());
                switch(xml.getStatus())
                {
                    case XML_DONE:
                        drawForecast();
                        Particle.publish("status", "forecast success");
                        break;
                    case  XML_TIMEOUT:
                    default:
                        Particle.publish("status", "forecast timeout");
                        hvac.addNotification( "No NDFD Update " );
                        FcstInterval = 5 * 60;    // retry in 5 mins
                        break;
                }
            }
        }

        if(millis() - lUpdateFcst >= (FcstInterval * 1000) )
        {
            GetForecast();
            UpdateEE();          // EE updates will go here for now (every 3 hours)
        }

        if(millis() - lUpdateOutTemp >= (5 * 60 * 1000) )        // update every 5 mins
        {
            displayOutTemp();
        }

        if(millis() - lBacklightTimer >= (blankSecs*1000) )     // screen blanker
        {
            Backlight(dimLevel);
            bScreenOn = false;
        }
    }
    delay(50);
}
