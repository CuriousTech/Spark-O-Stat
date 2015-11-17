//  Sparkostat script running on PngMagic  http://www.curioustech.net/pngmagic.html
//

	spark = 'https://api.particle.io/v1/devices/'
	deviceID = 'xxxxxxxxxxxxxxxxxxxxxxxx'
	token = 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'

	kwh = 3600 // killowatt hours (compressor+fan)
	ppkwh = 0.126  // price per KWH  (price / KWH)
	ppkwh  += 0.101 + 0.03 // surcharge 10.1%, school 3%, misc
	ccfs	= 0.70 / (60*60) // NatGas cost per hour divided into seconds

	modes = new Array('Off', 'Cool', 'Heat', 'Auto')

	btnX = 120
	btnY = 45
	btnW = 40

	if(Reg.overrideTemp == 0)
		Reg.overrideTemp = -1.2

	var settings

	streaming = false
	online = true

	Pm.Window('SparkRemote')

	Gdi.Width = 208 // resize drawing area
	Gdi.Height = 338

	Pm.ParticleListen()

	Pm.SetTimer(4*60*1000)
	OnTimer()

// Handle published events
function OnCall(msg, data)
{
	switch(msg)
	{
		case 'HTTPSTATUS':
			switch(+data)
			{
				case 12002: s = 'Timeout'; break
				case 12157: s =  'Enable Internet Options-> Advanced -> SSL2/3'; break
				default: s = ' '
			}
			Pm.Echo( ' Particle error: ' + data + ' ' + s)
			break
		case 'HTTPDATA':
			procLine(data)
			break
		case 'HTTPCLOSE':
			break

		case 'StreamStatus':
			Pm.Echo('Stream ' + data)
			streaming = +data
			break

		case 'BUTTON':
			switch(data)
			{
				case 0:		// Override
					SetVar('override', (ovrActive) ? 0:(Reg.overrideTemp * 10))
					break
				case 1:		// reset filter
					SetVar('resetfilter', 0)
					filterMins = 0
					break
				case 2:		// fan
					fanMode ^= 1; SetVar('fanmode', fanMode)
					break
				case 3:		// mode
					mode = (mode + 1) & 3; SetVar('mode', mode)
					break
				case 4:		// mode
					heatMode = (heatMode+1) % 3; SetVar('heatMode', heatMode)
					break
				case 5:		// Unused
					break
				case 6:		// cool H up
					setTemp(1, coolTempH + 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed())
					break
				case 7:		// cool H dn
					setTemp(1, coolTempH - 0.1, 1); SetVar('cooltemph', (coolTempH * 10).toFixed())
					break
				case 8:		// cool L up
					setTemp(1, coolTempL + 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed())
					break
				case 9:		// cool L dn
					setTemp(1, coolTempL - 0.1, 0); SetVar('cooltempl', (coolTempL * 10).toFixed())
					break
				case 10:		// heat H up
					setTemp(2, heatTempH + 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed())
					break
				case 11:		// heat H dn
					setTemp(2, heatTempH - 0.1, 1); SetVar('heattemph', (heatTempH * 10).toFixed())
					break
				case 12:		// heat L up
					setTemp(2, heatTempL + 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed())
					break
				case 13:		// heat L dn
					setTemp(2, heatTempL - 0.1, 0); SetVar('heattempl', (heatTempL * 10).toFixed())
					break
				case 14:		// thresh up
					if(cycleThresh < 6.3){ cycleThresh += 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
					break
				case 15:		// thresh dn
					if(cycleThresh > 0.1){ cycleThresh -= 0.1; SetVar('cyclethresh', (cycleThresh * 10).toFixed()); }
					break
				case 16:		// fanDelay up
					if(fanDelay < 255){ fanDelay += 10; SetVar('fanpostdelay', fanDelay); }
					break
				case 17:		// fanDelay dn
					if(fanDelay > 0){ fanDelay -= 10; SetVar('fanpostdelay', fanDelay); }
					break
				case 18:		// idleMin up
					idleMin++; SetVar('idlemin', idleMin)
					break
				case 19:		// idleMin dn
					idleMin--; SetVar('idlemin', idleMin)
					break
				case 20:		// cycleMin up
					cycleMin++; SetVar('cyclemin', cycleMin)
					break
				case 21:		// cycleMin dn
					cycleMin--; SetVar('cyclemin', cycleMin)
					break
				case 22:		// cycleMax up
					cycleMax+=60; SetVar('cyclemax', cycleMax)
					break
				case 23:		// cycleMax dn
					cycleMax--; SetVar('cyclemax', cycleMax)
					break
				case 24:		// override time up
				overrideTime+=60; SetVar('overridetime', overrideTime)
					break
				case 25:		// override time dn
					overrideTime-=10; SetVar('overridetime', overrideTime)
					break	
				case 26:		// override temp up
					Reg.overrideTemp += 0.1
					break
				case 27:		// override temp dn
					Reg.overrideTemp -= 0.1
					break
			}
			Draw()
			break

		case 'UPDATE':
			Draw()
			break

		default:
			Pm.Echo('SR Unrecognised ' + msg)
			break
	}
}

function SetVar(v, val)
{
	event = 'setvar'
	Http.Connect( particleUrl + deviceID + '/setvar?access_token=' + token, 'POST', 'params=' + v + ',' + val )
}

function procLine(data)
{
	if( !data.indexOf('}') ) // no event or bad format?
	{
		Pm.Echo('Particle: Unexpected data: ' + data)
		return
	}

//Pm.Echo('Line: ' + data)
	Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			data.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + data + ')')

	switch(event)
	{
		case 'settings':
			settings = Json.return_value

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

			event = 'settings_result'
			Http.Connect( particleUrl + deviceID + '/result?access_token=' + token )
			break

		case 'settings_result':
			str = Json.result
			Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
				str.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + str + ')')

			coolTempL = +Json.c0 / 10
			coolTempH = +Json.c1 / 10
			heatTempL = +Json.h0 / 10
			heatTempH = +Json.h1 / 10
			inTemp = +Json.it / 10
			targetTemp = +Json.tt / 10
			idleMin = +Json.im
			cycleMin = +Json.cn
			cycleMax = +Json.cx
			filterMins = +Json.fh
			outTemp = +Json.ot / 10
			outFL = +Json.ol
			outFH = +Json.oh
			cycleTimer = +Json.ct
			fanTimer = +Json.ft
			runTotal = +Json.rt
			tempDiffTotal = +Json.td
			overrideTime = +Json.ov
			rh = +Json.rh / 10
			remoteTimer = Json.rm
			remoteTimeout = Json.ro

			stat = 0
			if(running)
				stat = (mode == 3) ? autoMode : mode // if(auto) stat = heat/cool
			if(stat == 2 && heatMode == 0) // if (heat & heatMode == NG)
				stat = 3

			LogTemps( stat, fan, inTemp, targetTemp, rh )
			if(Pm.FindWindow( 'HVAC' ))
			{
				Pm.History( 'REFRESH' )
			}
			Draw()

			if(Json.lc) // see if there are logs
			{
				event = 'log'
				Http.Connect( particleUrl + deviceID + '/getvar?access_token=' + token, 'POST', 'params=log' )
			}
			if(!streaming)
			{
				Pm.ParticleListen('START')
			}
			break

		case 'log':
			logCount = Json.return_value
//			Pm.Echo('log ' + Json.return_value)
			if(logCount)
			{
				event = 'logres'
				Http.Connect( particleUrl + deviceID + '/result?access_token=' + token )
			}
			break

		case 'logres':
			str = Json.result
			Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
				str.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + str + ')')

			// time mode secs t1  rh1 t2  rh2

			if(logCount > 1)
			{
				event = 'log'
				Http.Connect( particleUrl + deviceID + '/getvar?access_token=' + token, 'POST', 'params=log' )
			}
			break

		case 'setvar':
//			Pm.Echo('setvar ' + Json.return_value)
			break

		default:
			Pm.Echo('SR Unknown event: ' + event)
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
	event = 'settings'
	Http.Connect( particleUrl + deviceID + '/getvar?access_token=' + token, 'POST', 'params=settings' )
}

function Draw()
{
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

	Gdi.Brush( online ? Gdi.Argb(255, 25, 25, 255) : Gdi.Argb(255, 255, 0, 0) )
	Gdi.Text( 'X', Gdi.Width-17, 1 )

	Gdi.Font( 'Arial' , 13, 'Regular')
	Gdi.Brush( Gdi.Argb(255, 255, 255, 255) )

	date = new Date()
	Gdi.Text( date.toLocaleTimeString(), Gdi.Width-84, 2 )

	x = 5
	y = 22

	Gdi.Text('In: ' + inTemp + '°', x, y)
	Gdi.Text( '>' + targetTemp + '°  ' + rh + '%', x + 54, y)

	Gdi.Text('O:' + outTemp + '°', x + 150, y)

	y = btnY
	Gdi.Text('Fan:', x, y); 	Gdi.Text(fan ? "On" : "Off", x + 100, y, 'Right')
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
	Gdi.Text('Fan Delay:', x, y); Gdi.Text(fanDelay , x + 112, y, 'Time')
	y += bh
	Gdi.Text('Idle Min:', x, y); 	Gdi.Text(idleMin , x + 112, y, 'Time')
	y += bh
	Gdi.Text('cycle Min:', x, y); 	Gdi.Text(cycleMin, x + 112, y, 'Time')
	y += bh
	Gdi.Text('cycle Max:', x, y); 	Gdi.Text(cycleMax , x + 112, y, 'Time')
	y += bh
	Gdi.Text('ovr Time:', x, y); 	Gdi.Text(overrideTime , x + 112, y, 'Time')
	y += bh
	a = Reg.overrideTemp
	Gdi.Text('ovr Temp:', x, y);  Gdi.Text(a + '°' , x + 112, y, 'Right')

	if(ovrActive)
		Gdi.Pen(Gdi.Argb(255,255,20,20), 2 )	// Button square
	else
		Gdi.Pen(Gdi.Argb(255,20,20,255), 2 )	// Button square
	Gdi.Rectangle(x, y, 64, 15, 2)
	Pm.Button(x, y, 64, 15)

	y = Gdi.Height - 36

 	if(mode == 1 || (mode==2 && heatMode == 0))  // cool or HP
		cost = ppkwh * runTotal / (1000*60*60) * kwh
	else
		cost = ccfs * runTotal

	Gdi.Text('Filter:', x, y);  Gdi.Text(filterMins*60, x + 90, y, 'Time')
	Gdi.Pen(Gdi.Argb(255,20,20,255), 2 )	// Button square
	Pm.Button(x, y, 90, 15)
	Gdi.Rectangle(x, y, 90, 15, 2)
	Gdi.Text('Cost:', x+100, y); 	Gdi.Text( '$' +cost.toFixed(2) , x + 190, y, 'Right')

	y += bh
	Gdi.Text('Cyc:', x, y); 	Gdi.Text( cycleTimer, x + 90, y, 'Time')
	Gdi.Text('Tot:', x+100, y); 	Gdi.Text(runTotal, x + 190, y, 'Time')

	heatModes = Array('HP', 'NG', 'Auto')
	buttons = Array(fanMode ? 'On' : 'Auto', modes[mode],
		heatModes[heatMode], ' ',
		'+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-', '+', '-' )

	for (n = 0, row = 0; row < buttons.length / 2; row++)
	{
		for (col = 0; col < 2; col++)
		{
			x = btnX + (col * btnW)
			y = btnY + (row * 19)
			drawButton(buttons[n++], x, y, btnW, 18)
		}
	}
}

function drawButton(text, x, y, w, h)
{
	Gdi.GradientBrush( 0,y, 22, 24, Gdi.Argb(200, 200, 200, 255), Gdi.Argb(200, 60, 60, 255 ), 90)
	Gdi.FillRectangle( x, y, w-2, h, 3)
	ShadowText( text, x+(w/2), y, Gdi.Argb(255, 255, 255, 255) )
	Pm.Button(x, y, w, h)
}

function ShadowText(str, x, y, clr)
{
	Gdi.Brush( Gdi.Argb(255, 0, 0, 0) )
	Gdi.Text( str, x+1, y+1, 'Center')
	Gdi.Brush( clr )
	Gdi.Text( str, x, y, 'Center')
}

function LogHVAC( uxt, state, fan )
{
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	tf = fso.OpenTextFile( 'hvacdata.log', 8, true)
	tf.WriteLine( uxt + ',' + state + ',' + fan )
	tf.Close()
	fso = null
}

function LogTemps( stat, fan, inTemp, targetTemp, inrh )
{
	if(targetTemp == 0)
		return
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	date = new Date()

	tf = fso.OpenTextFile( 'stattemp.log', 8, true)
	tf.WriteLine( Math.floor( date.getTime() / 1000 ) + ',' + stat + ',' + fan + ',' + inTemp + ',' + targetTemp +',' + inrh )
	tf.Close()
	fso = null
}

function isoDateToJsDate(value) 
{
	var a = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}(?:\.\d*)?)(?:([\+-])(\d{2})\:(\d{2}))?Z?$/.exec(value)
	if (a)
	{
		var utcMilliseconds = Date.UTC(+a[1], +a[2] - 1, +a[3], +a[4], +a[5], +a[6])
		if( a[7] == '-' ) utcMilliseconds += a[8] * (1000 * 60 * 60)
		else utcMilliseconds -= a[8] * (1000 * 60 * 60)
		return new Date(utcMilliseconds)
	}
	return value
}
