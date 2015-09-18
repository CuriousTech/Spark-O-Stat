//  Sparkostat script running on PngMagic  http://www.curioustech.net/pngmagic.html
//

	spark = 'https://api.particle.io/v1/devices/'
	deviceID = 'xxxxxxxxxxxxxxxxxxxxxxxx'
	token = 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'

	Pm.Echo()

	eventUrl = spark + deviceID + '/events?access_token=' + token

	kwh = 3600 // killowatt hours (compressor+fan)
	ppkwh = 0.11593 // price per KWH
	ppkwh  += 0.01494 + 0.00392 // surcharge, school, misc

	modes = new Array('Off', 'Cool', 'Heat', 'Auto')

	btnX = 120
	btnY = 45
	btnW = 40

	if(Reg.overrideTemp == 0)
		Reg.overrideTemp = -1.2

	var cnt = (Math.random(100) * 10000).toFixed() // fix for cache

	var settings

	readingLogs = false
	streaming = false

	OnTimer()

function OnClick(x, y) // when window is clicked
{
	Pm.Window( 'SparkRemote' )

	x = Math.floor( (x - btnX) / btnW)
	y = Math.floor( ((y - btnY) / 19))

	if(y < 0) OnTimer() // reload if click above buttons
	if(y < 0 || x < 0 || y>26 || x >3) return

	code = (y*2)+x

	switch(code)
	{
		case 0:		// fan
			fanMode ^= 1; SetVar('fanmode', fanMode)
			break
		case 1:		// mode
			mode = (mode + 1) & 3; SetVar('mode', mode)
			break
		case 2:		// mode
			heatMode = (heatMode+1) % 3; SetVar('heatMode', heatMode)
			break
		case 3:		// Override
			SetVar('override', Reg.overrideTemp * 10)
			break
		case 4:		// cool H up
			setTemp(1, coolTempH + 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed())
			break
		case 5:		// cool H dn
			setTemp(1, coolTempH - 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed())
			break
		case 6:		// cool L up
			setTemp(1, coolTempL + 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed())
			break
		case 7:		// cool L dn
			setTemp(1, coolTempL - 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed())
			break
		case 8:		// heat H up
			setTemp(2, heatTempH + 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed())
			break
		case 9:		// heat H dn
			setTemp(2, heatTempH - 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed())
			break
		case 10:		// heat L up
			setTemp(2, heatTempL + 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed())
			break
		case 11:		// heat L dn
			setTemp(2, heatTempL - 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed())
			break
		case 12:		// thresh up
			if(cycleThresh < 6.3){ cycleThresh += 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
			break
		case 13:		// thresh dn
			if(cycleThresh > 0.1){ cycleThresh -= 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
			break
		case 14:		// fanDelay up
			if(fanDelay < 255){ fanDelay += 10; SetVar('fanpostdelay', fanDelay); }
			break
		case 15:		// fanDelay dn
			if(fanDelay > 0){ fanDelay -= 10; SetVar('fanpostdelay', fanDelay); }
			break
		case 16:		// idleMin up
			idleMin++; SetVar('idlemin', idleMin)
			break
		case 17:		// idleMin dn
			idleMin--; SetVar('idlemin', idleMin)
			break
		case 18:		// cycleMin up
			cycleMin++; SetVar('cyclemin', cycleMin)
			break
		case 19:		// cycleMin dn
			cycleMin--; SetVar('cyclemin', cycleMin)
			break
		case 20:		// cycleMax up
			cycleMax+=60; SetVar('cyclemax', cycleMax)
			break
		case 21:		// cycleMax dn
			cycleMax--; SetVar('cyclemax', cycleMax)
			break
		case 22:		// override time up
			overrideTime+=60; SetVar('overridetime', overrideTime)
			break
		case 23:		// override time dn
			overrideTime-=10; SetVar('overridetime', overrideTime)
			break	
		case 24:		// override temp up
			Reg.overrideTemp += 0.1
			break
		case 25:		// override temp dn
			Reg.overrideTemp -= 0.1
			break
	}

	Draw()
	Pm.SetTimer(3*1000) // request update in 3 seconds
}

// Handle published events (sometimes event and JSON come in 2 parts)
function OnCall(msg, data)
{
	switch(msg)
	{
		case 'HTTPDATA':
			if(data.length == 1) break // keep-alive heartbeat
//			Pm.Echo( ' Particle data(' + data.length + '): ' + data)
			if(data == ':ok\n\n' )
			{
				Pm.Echo( ' Particle stream started')
				break
			}

			if(data.indexOf('event') >= 0) // possibly just the event name
			{
				lines = data.split('\n')
				event = lines[0].substr( lines[0].indexOf(':') + 2)
				Pm.Echo('Event: '+  event.length + ' ' + event)
			}

			if( data.indexOf('}') > 0 ) // event+data or just data
			{
				data = data.substr( data.indexOf('{') ) // remove extra

				Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
						data.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + data + ')')

				switch(event)
				{
					case 'spark/status':
						Pm.Echo('Status ' + Json.data)
						break
					case 'spark/cc3000-patch-version':
						Pm.Echo('Version update: ' + Json.data)
						break
					case 'modeChg':
						str = Json.data
						Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
							str.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + str + ')')
						Pm.Echo('Mode ' + Json.Mode)
						Pm.Echo('Run ' + Json.Run)
						Pm.Echo('Fan ' + Json.Fan)
						OnTimer()  // read the rest of the data
						break
					default:
						Pm.Echo('Unknown event: ' + event)
						break
				}
			}
			break
		case 'HTTPCLOSE':
			Pm.Echo( 'Particle disconected' )
			streaming = false
			Pm.SetTimer(20*1000)		// reconnect in 20 secs
			break
		default:
			Pm.Echo('SR Unrecognised ' + msg)
			break
	}
}

// mimic thermostat
function setTemp( mode, Temp, hl)
{
	if(mode == 3) // auto
	{
		mode = autoMode
	}

	switch(mode)
	{
		case 1:
			if(Temp < 65.0 || Temp > 88.0)    // ensure sane values
				break
			if(hl)
			{
				coolTempH = Temp
				coolTempL = Math.min(coolTempH, coolTempL)     // don't allow h/l to invert
			}
			else
			{
				coolTempL = Temp
				coolTempH = Math.max(coolTempL, coolTempH)
			}
			save = heatTempH - heatTempL
			heatTempH = Math.min(coolTempL - 2, heatTempH) // Keep 2.0 degree differencial for Auto mode
			heatTempL = heatTempH - save                      // shift heat low by original diff
			break
		case 2:
			if(Temp < 63.0 || Temp > 86.0)    // ensure sane values
				break
			if(hl)
			{
				heatTempH = Temp
				heatTempL = Math.min(heatTempH, heatTempL)
			}
			else
			{
				heatTempL = Temp;
				heatTempH = Math.max(heatTempL, heatTempH);
			}
			save = coolTempH - coolTempL;
			coolTempL = Math.max(heatTempH - 2, coolTempL);
			coolTempH = coolTempL + save;
			break
	}
}

function OnTimer()
{
	Pm.SetTimer(4*60*1000)

	if(!readingLogs)
	{	if( !TestSpark() )	// this gets the settings var
		{
			Pm.Echo( 'Spark not found' )
			Pm.Quit()
		}

//Pm.Echo('settings ' + settings)

		mode = settings & 3
		autoMode = (settings >> 2) & 3
		heatMode = (settings >> 4) & 3
		fanMode = (settings >> 6) & 1
		running = (settings>> 7) & 1
		fan = (settings>> 8) & 1
		ovrActive = (settings>> 9) & 1
		eHeatThresh = (settings >> 10) & 0x3F
		fanDelay = (settings >> 16) & 0xFF
		cycleThresh = ((settings >> 24) & 0xFF) / 10

		res = ReadVar( 'result' )	// result set by settings call

//Pm.Echo('settings ' + res)
		Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			res.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + res + ')')

		coolTempL = +Json.c0 / 10
		coolTempH = +Json.c1 / 10
		heatTempL = +Json.h0 / 10
		heatTempH = +Json.h1 / 10
		inTemp = +Json.it / 10
		targetTemp = +Json.tt / 10
		idleMin = +Json.im
		cycleMin = +Json.cn
		cycleMax = +Json.cx
		filterHours = +Json.fh
		outTemp = +Json.ot / 10
		outFL = +Json.ol
		outFH = +Json.oh
		cycleTimer = +Json.ct
		fanTimer = +Json.ft
		runTotal = +Json.rt
		tempDiffTotal = +Json.td
		overrideTime = +Json.ov
		rh = +Json.rh / 10

		Draw()

		LogTemps( inTemp, targetTemp, rh )
		if(Pm.FindWindow( 'HVAC' ))
		{
			Pm.History( 'REFRESH' )
		}
	}

	if( GetVar( 'log' ) )
	{
		res = ReadVar( 'result' )

//Pm.Echo(' ' )
//Pm.Echo('log ' + res)
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

		if( !streaming )
			if( Http.Connect( eventUrl ) ) // Start the event stream
				streaming = true
 	}
}

function Draw()
{
	Pm.Window('SparkRemote')

	Gdi.Width = 208 // resize drawing area
	Gdi.Height = 338

	Gdi.Clear(0) // transaprent

	// rounded window
	Gdi.Brush( Gdi.Argb( 160, 0, 0, 0) )
	Gdi.FillRectangle(0, 0, Gdi.Width-1, Gdi.Height-1, 6)
	Gdi.Pen( Gdi.Argb(255, 0, 0, 255), 1 )
	Gdi.Rectangle(0, 0, Gdi.Width-1, Gdi.Height-1, 6)

	// Title
	Gdi.Font( 'Courier New', 15, 'BoldItalic')
	Gdi.Brush( Gdi.Argb(255, 255, 230, 25) )
	Gdi.Text( 'Spark-O-Stat', 5, 1 )

	Gdi.Brush( Gdi.Argb(255, 255, 0, 0) )
	Gdi.Text( 'X', Gdi.Width-17, 1 )

	Gdi.Pen(Gdi.Argb(255,0,0,1), 1 )
	Gdi.Font( 'Arial' , 13, 'Regular')
	Gdi.Brush( Gdi.Argb(255, 255, 255, 255) )

	date = new Date()
	Gdi.Text( date.toLocaleTimeString(), Gdi.Width-84, 2 )

	x = 5
	y = 22

	Gdi.Text('In: ' + inTemp + '°', x, y)
	Gdi.Text( '>' + targetTemp + '°  ' + rh + '%', x + 54, y)

	Gdi.Text('O:' + outTemp + '°', x + 150, y)
//	Gdi.Text(outTemp + '°', x + 162, y)

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
	y += bh
	Gdi.Text('ovr Time:', x, y); 	Gdi.Text(secsToTime(overrideTime) , x + 56, y)
	y += bh
	a = Reg.overrideTemp
	Gdi.Text('ovr Temp:', x, y);  Gdi.Text(a + '°' , x + 112, y, 'Right')

	y = Gdi.Height - 35

	cost = ppkwh * runTotal / (1000*60*60) * kwh

	Gdi.Text('Filter:', x, y); 	Gdi.Text(filterHours + ' h' , x + 36, y)
	Gdi.Text('Cost:', x+90, y); 	Gdi.Text( '$' +cost.toFixed(2) , x + 130, y)

	y += bh
	Gdi.Text('Cyc:', x, y); 	Gdi.Text(secsToTime(cycleTimer) , x + 30, y)
	Gdi.Text('Tot:', x+90, y); 	Gdi.Text(secsToTime(runTotal) , x + 120, y)

	heatModes = Array('HP', 'NG', 'Auto')
	buttons = Array(fanMode ? 'On' : 'Auto', modes[mode],
		heatModes[heatMode], 'Ovrd',
		'+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-' )

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
	neg = ' '
	if(elap < 0)
	{
		elap = -elap
		neg = '-'
	}
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
			return '   ' + neg + m +':'+s
		}
	}
	if( m < 10) m = '0' + m
	if( h < 10) h = '  ' + h
	if(d) return d + 'd ' + h + 'h'
	return neg + h+':'+m+':'+s
}

function getLogs()
{
	while( GetVar( 'log' ) )
	{
		res = ReadVar( 'result' )

//Pm.Echo('log ' + res)
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

function LogTemps( inTemp, targetTemp, inrh )
{
	if(targetTemp == 0)
		return
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	date = new Date()

	tf = fso.OpenTextFile( 'stattemp.log', 8, true)
	tf.WriteLine( date + ',' + inTemp + ',' + targetTemp +',' + inrh )
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
	xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded')
	Pm.SetTimer(4*60*1000)
	xhr.send( 'params=settings' )
	cnt++
	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	settings = JsonObj.return_value

//		Pm.Echo('JSON ' + xhr.responseText)

	xhr = null
	return settings
}

function ReadVar(varName)
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('GET', spark + deviceID + '/' + varName + '?access_token=' + token + '&r=' + cnt, false)
	xhr.onreadystatechange = testcb
	xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded')
	xhr.send()

	cnt++
//	Pm.Echo('JSON ' + xhr.responseText)

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	xhr = null
//	Pm.Echo( 'Var = ' + JsonObj.result )
//	Pm.Echo( 'Length = ' + JsonObj.result.length )

	return JsonObj.result
}

function GetVar(name)
{
	xhr = new ActiveXObject( 'Microsoft.XMLHTTP' )
	xhr.open('POST', spark + deviceID + '/getvar?access_token=' + token, false)
	xhr.onreadystatechange = testcb
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
	xhr.onreadystatechange = testcb
	xhr.setRequestHeader('content-type', 'application/x-www-form-urlencoded')
	xhr.send( 'params=' + v + ',' + val )

//	Pm.Echo('JSON ' + xhr.responseText)

	var JsonObj = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
		xhr.responseText.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + xhr.responseText + ')')

	xhr = null
//	Pm.Echo( 'returnvalue = ' + JsonObj.return_value )

	return JsonObj.return_value
}
