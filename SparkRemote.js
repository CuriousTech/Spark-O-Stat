//  Sparkostat script running on PngMagic  http://www.curioustech.net/pngmagic.html
//
	spark = 'https://api.spark.io/v1/devices/'
	deviceID = 'xxxxxxxxxxxxxxx'
	token = 'xxxxxxxxxxxxxxxxxxxxxxxx'

	Pm.Echo()

	modes = new Array('Off', 'Cool', 'Heat', 'Auto')

	btnX = 128
	btnY = 45
	btnW = 40

	var cnt = (Math.random(100) * 10000).toFixed() // fix for cache

	var settings

	readingLogs = false

	OnTimer()

//	SetVar('notify', 'Test 1')

function OnClick(x, y) // when window is clicked
{
	Pm.Window( 'SparkIO' )

	x = Math.floor( (x - btnX) / btnW)
	y = Math.floor( (y - btnY) / 19)

	if(y < 0 || x < 0 || y>4 || x >3) return

	code = (y*2)+x

	switch(code)
	{
		case 0:		// fan
			fanMode ^= 1; SetVar('fanmode', fanMode);
			break;
		case 1:		// mode
			mode = (mode + 1) & 3; SetVar('mode', mode);
			break;
		case 2:		// mode
			heatMode ^= 1; SetVar('heatMode', heatMode);
			break;
		case 4:		// cool H up
			setTemp(1, coolTempH + 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed());
			break;
		case 5:		// cool H dn
			setTemp(1, coolTempH - 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed());
			break;
		case 6:		// cool L up
			setTemp(1, coolTempL + 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed());
			break;
		case 7:		// cool L dn
			setTemp(1, coolTempL - 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed());
			break;
		case 8:		// heat H up
			setTemp(2, heatTempH + 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed());
			break;
		case 9:		// heat H dn
			setTemp(2, heatTempH - 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed());
			break;
		case 10:		// heat L up
			setTemp(2, heatTempL + 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed());
			break;
		case 11:		// heat L dn
			setTemp(2, heatTempL - 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed());
			break
		case 12:		// thresh up
			if(cycleThresh < 6.3){ cycleThresh += 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
			break
		case 13:		// thresh dn
			if(cycleThresh > 0.1){ cycleThresh -= 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
			break
		case 14:		// fanDelay up
			if(fanDelay < 255){ fanDelay++; SetVar('fandelay', fanDelay); }
			break
		case 15:		// fanDelay dn
			if(fanDelay > 0){ fanDelay --; SetVar('fandelay', fanDelay); }
			break
		case 16:		// idleMin up
			idleMin++; SetVar('idlemin', idleMin)
			break
		case 17:		// idleMin dn
			idleMin--; SetVar('idlemin', idleMin)
			break;
		case 18:		// cycleMin up
			cycleMin++; SetVar('cyclemin', cycleMin)
			break;
		case 19:		// cycleMin dn
			cycleMin--; SetVar('cyclemin', cycleMin)
			break;
		case 20:		// cycleMax up
			cycleMax++; SetVar('cyclemax', cycleMax)
			break;
		case 21:		// cycleMax dn
			cycleMax--; SetVar('cyclemax', cycleMax)
			break;
	}

	Draw()
}

// mimic thermostat
function setTemp( mode, Temp, hl)
{
	if(mode == 3) // auto
	{
		mode = autoMode;
	}

	switch(mode)
	{
		case 1:
			if(Temp < 65.0 || Temp > 88.0)    // ensure sane values
				break;
			if(hl)
			{
				coolTempH = Temp;
				coolTempL = Math.min(coolTempH, coolTempL);     // don't allow h/l to invert
			}
			else
			{
				coolTempL = Temp;
				coolTempH = Math.max(coolTempL, coolTempH);
			}
			save = heatTempH - heatTempL;
			heatTempH = Math.min(coolTempL - 2, heatTempH); // Keep 2.0 degree differencial for Auto mode
			heatTempL = heatTempH - save;                      // shift heat low by original diff
			break;
		case 2:
			if(Temp < 63.0 || Temp > 86.0)    // ensure sane values
				break;
			if(hl)
			{
				heatTempH = Temp;
				heatTempL = Math.min(heatTempH, heatTempL);
			}
			else
			{
				heatTempL = Temp;
				heatTempH = Math.max(heatTempL, heatTempH);
			}
			save = coolTempH - coolTempL;
			coolTempL = Math.max(heatTempH - 2, coolTempL);
			coolTempH = coolTempL + save;
			break;
	}
}

function OnTimer()
{
	Pm.Echo()
	Pm.SetTimer(5*60*1000)
	if(!readingLogs)
	{	if( !TestSpark() )	// this gets the settings var
		{
			Pm.Echo( 'Spark not found' )
			Pm.Quit()
		}

Pm.Echo('settings ' + settings)

		mode = settings & 3
		autoMode = (settings >> 2) & 3
		heatMode = (settings >> 4) & 1
		fanMode = (settings >> 5) & 1
		running = (settings>> 6) & 1
		fan = (settings>> 8) & 1
		eHeatThresh = (settings >> 8) & 0x3F
		fanDelay = (settings >> 14) & 0xFF
		cycleThresh = (settings >> 22) & 0xFF

		res = ReadVar( 'result' )	// result set by settings call

Pm.Echo('settings ' + res)
		Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			res.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + res + ')')

		coolTempL = +Json.c0 / 10
		coolTempH = +Json.c1 / 10
		heatTempL = +Json.h0 / 10
		heatTempH = +Json.h1 / 10
		inTemp = +Json.it / 10
		targetTemp = +Json.tt / 10
		idleMin =+ Json.im
		cycleMin = +Json.cn
		cycleMax = +Json.cx
		filterHours = +Json.fh
		outTemp = Json.ot / 10
		outFL = +Json.ol
		outFH = +Json.oh
		cycleTimer = +Json.ct
		fanTimer = +Json.ft
		runTotal = +Json.rt
		tempDiffTotal = +Json.td

		Draw()

		LogTemps( inTemp, targetTemp )
	}

	if( GetVar( 'log' ) )
	{
		res = ReadVar( 'result' )

Pm.Echo(' ' )
Pm.Echo('log ' + res)
		JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			res.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + res + ')')
		LogHVAC( JsonObj.time, JsonObj.mode, JsonObj.secs, JsonObj.t1 / 10, JsonObj.rh1 / 10, JsonObj.t2 / 10, JsonObj.rh2 / 10 )
		Pm.SetTimer(1000)	// keep reading more
		readingLogs = true
	}
	else
	{
//		Pm.SetTimer(60*1000)
		readingLogs = false
 	}
}

function Draw()
{
	Pm.Window('SparkIO')

	Gdi.Width = 208 // resize drawing area
	Gdi.Height = 280

	Gdi.Clear(0) // transaprent

	// rounded window
	Gdi.Brush( Gdi.Argb( 160, 0, 0, 0) )
	Gdi.FillRectangle(0, 0, Gdi.Width-1, Gdi.Height-1, 6)
	Gdi.Pen( Gdi.Argb(255, 0, 0, 255), 1 )
	Gdi.Rectangle(0, 0, Gdi.Width-1, Gdi.Height-1, 6)

	// Title
	Gdi.Font( 'Courier New', 15, 'BoldItalic')
	Gdi.Brush( Gdi.Argb(255, 255, 230, 25) )
	Gdi.Text( 'Spark-O-Stat', 10, 1 )

	Gdi.Brush( Gdi.Argb(255, 255, 0, 0) )
	Gdi.Text( 'X', Gdi.Width-17, 1 )

	Gdi.Pen(Gdi.Argb(255,0,0,1), 1 )
	Gdi.Font( 'Arial' , 13, 'Regular')
	Gdi.Brush( Gdi.Argb(255, 255, 255, 255) )

	date = new Date()

	m = date.getMinutes()
	if(m<10) m = '0'+m
	Gdi.Text( date.getHours() + ':'+m, Gdi.Width-54, 1 )

	x = 5
	y = 22

	Gdi.Text('In:', x, y)
	Gdi.Text(inTemp + '°', x + 20, y)

	Gdi.Text('Trg:', x + 65, y)
	Gdi.Text(targetTemp + '°', x + 93, y)

	Gdi.Text('Out:', x + 134, y)
	Gdi.Text(outTemp + '°', x + 162, y)

	y = btnY
	Gdi.Text('Fan:', x, y)
	Gdi.Text(fan ? "On" : "Off", x + 100, y, 'Right')
	y += 20

	s = 'huh'
	switch(mode)
	{
		case 1: s = 'Cooling'; break
		case 2: s = 'Heating'; break
		case 3: s = 'eHeating'; break
	}

	bh = 19

	Gdi.Text('Run:', x, y)
	Gdi.Text(running ? s : "Off", x + 100, y, 'Right')
	y += bh

	Gdi.Text('Cool Hi:', x, y); 	Gdi.Text(coolTempH.toFixed(1) + '°', x + 112, y, 'Right')
	y += bh
	Gdi.Text('Cool Lo:', x, y); 	Gdi.Text(coolTempL.toFixed(1) + '°', x + 112, y, 'Right')
	y += bh
	Gdi.Text('Heat Hi:', x, y); 	Gdi.Text(heatTempH.toFixed(1) + '°', x + 112, y, 'Right')
	y += bh
	Gdi.Text('Heat Lo:', x, y); 	Gdi.Text(heatTempL.toFixed(1) + '°', x + 112, y, 'Right')
	y += bh
	Gdi.Text('Threshold:', x, y); 	Gdi.Text(cycleThresh.toFixed(1) + '°', x + 112, y, 'Right')
	y += bh
	Gdi.Text('Fan Delay:', x, y); Gdi.Text(secsToTime(fanDelay) , x + 56, y)
	y += bh
	Gdi.Text('Idle Min:', x, y); 	Gdi.Text(secsToTime(idleMin) , x + 56, y)
	y += bh
	Gdi.Text('cycle Min:', x, y); 	Gdi.Text(secsToTime(cycleMin), x + 56, y)
	y += bh
	Gdi.Text('cycle Max:', x, y); 	Gdi.Text(secsToTime(cycleMax) , x + 56, y)

	y += bh+2
	Gdi.Text('Cyc:', x, y); 	Gdi.Text(secsToTime(cycleTimer) , x + 30, y)
	Gdi.Text('Tot:', x+90, y); 	Gdi.Text(secsToTime(runTotal) , x + 120, y)

	buttons = Array(fanMode ? 'On' : 'Auto', modes[mode],
		heatMode ? 'NG' : 'HP', '  ',
		'+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-' )

	for (n = 0, row = 0; row < buttons.length / 2; row++)
	{
		for (col = 0; col < 2; col++)
		{
			x = btnX + (col * btnW)
			y = btnY + (row * 19)

			Gdi.GradientBrush( 0,y, 22, 24, Gdi.Argb(200, 200, 200, 255), Gdi.Argb(200, 60, 60, 255 ), 90)
			Gdi.FillRectangle( x, y, btnW-2, 18, 3)
			clr = Gdi.Argb(255, 255, 255, 255) // rest of the days = white
			ShadowText( buttons[n++], x+(btnW/2), y, clr)
		}
	}
}

function ShadowText(str, x, y, clr)
{
	Gdi.Brush( Gdi.Argb(255, 0, 0, 0) )
	Gdi.Text( str, x+1, y+1, 'Center')
	Gdi.Brush( clr )
	Gdi.Text( str, x, y, 'Center')
}

function secsToTime( elap )
{
	d = 0
	m = 0
	h   = Math.floor(elap / 3600)
	if(h >23)
	{
		d = Math.floor(h / 24)
		h -= (d*24)
	}
	else
	{
		m = Math.floor((elap - (h * 3600)) / 60)
		s = elap - (h * 3600) - (m * 60)
		if( s < 10) s = '0' + s
		if(h == 0)
		{
			if( m < 10) m = '  ' + m
			return '    ' + m +':'+s
		}
	}
	if( m < 10) m = '0' + m
	if( h < 10) h = '  ' + h
	if(d) return d + 'd ' + h + 'h'
	return h+':'+m+':'+s
}

function getLogs()
{
	for(;;)
	{
		if( !GetVar( 'log' ) )
			return;
		res = ReadVar( 'result' )

Pm.Echo('log ' + res)
		JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			res.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + res + ')')
		LogHVAC( JsonObj.time, JsonObj.mode, JsonObj.secs, JsonObj.t1 / 10, JsonObj.rh1 / 10, JsonObj.t2 / 10, JsonObj.rh2 / 10 )
	}
}

function LogHVAC( uxt, mode, secs, t1, rh1, t2, rh2 )
{
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	date = new Date( uxt * 1000 )

	tf = fso.OpenTextFile( 'hvacdata.log', 8, true)
	tf.WriteLine( date + ',' +mode + ',' + secs +',' + t1 + ',' + rh1 + ',' + t2 + ',' + rh2 )
	tf.Close()
	fso = null
}

function LogTemps( inTemp, targetTemp )
{
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	date = new Date()

	tf = fso.OpenTextFile( 'stattemp.log', 8, true)
	tf.WriteLine( date + ',' + inTemp + ',' + targetTemp )
	tf.Close()
	fso = null
}


function testcb()
{
}

function TestSpark()	// async call for testing net connection so it doesn't freeze
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('POST', spark + deviceID + '/getvar?access_token=' + token, false)
	xhr.onreadystatechange = testcb
	xhr.setRequestHeader('content-type', 'application/x-www-form-urlencoded')
	Pm.SetTimer(2*60*1000)
	xhr.send( 'params=settings' )

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	settings = JsonObj.return_value
	res = JsonObj.connected

//		Pm.Echo('JSON ' + xhr.responseText)

	xhr = null
	return res
}

function ReadVar(varName)
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('GET', spark + deviceID + '/' + varName + '?access_token=' + token + '&r=' + cnt, false)
	xhr.setRequestHeader('content-type', 'application/x-www-form-urlencoded')
	xhr.send()

	cnt++
//	Pm.Echo('JSON ' + xhr.responseText)

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	xhr = null
	Pm.Echo( 'Var = ' + JsonObj.result )
	Pm.Echo( 'Length = ' + JsonObj.result.length )

	return JsonObj.result
}

function GetVar(name)
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('POST', spark + deviceID + '/getvar?access_token=' + token, false)
	xhr.setRequestHeader('content-type', 'application/x-www-form-urlencoded')
	xhr.send( 'params=' + name )

//	Pm.Echo('JSON ' + xhr.responseText)

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	xhr = null
//	Pm.Echo( 'returnvalue = ' + JsonObj.return_value )

	return JsonObj.return_value
}

function SetVar(v,  val)
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('POST', spark + deviceID + '/setvar?access_token=' + token, false)
	xhr.setRequestHeader('content-type', 'application/x-www-form-urlencoded')
	xhr.send( 'params=' + v + ',' + val )

//	Pm.Echo('JSON ' + xhr.responseText)

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	xhr = null
//	Pm.Echo( 'returnvalue = ' + JsonObj.return_value )

	return JsonObj.return_value
}
