// Particle stream listener for SparkRemote

  spark = 'https://api.particle.io/v1/devices/'
  deviceID = 'xxxxxxxxxxxxxxxxxxxxxxxx'
  token = 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'

  eventUrl = spark + deviceID + '/events?access_token=' + token

// Handle published events
function OnCall(msg, data)
{
	switch(msg)
	{
		case 'START':
			if(Http.Connected)
			{
				Pm.SparkRemote( 'StreamStatus', 1)
				Pm.SparkRemote( 'CloudStatus', 1) // assume it's connected
				break
			}
			if( Http.Connect( eventUrl ) ) // Start the event stream
					Pm.SparkRemote( 'StreamStatus', 1)
			else
					Pm.Echo('Stream failed')
			Pm.SetTimer(60*1000)		// recheck every 60 seconds
			break
		case 'HTTPDATA':
//			Pm.Echo('Data ' + data)
			heartbeat = new Date()
			if(data.length <= 2) break // keep-alive heartbeat
			lines = data.split('\n')
			for(i = 0; i < lines.length; i++)
				procLine(lines[i])
			break
		case 'HTTPCLOSE':
			Pm.SparkRemote( 'StreamStatus', 0)
			break
		case 'HTTPSTATUS':
			break
		default:
			Pm.Echo('PL Unrecognised ' + msg)
			break
	}
}

function OnTimer()
{
	if(Http.Connected)
		return
	if( Http.Connect( eventUrl ) ) // Start the event stream
		Pm.SparkRemote( 'StreamStatus', 1)
	else
		Pm.Echo('Stream failed')
}

function procLine(data)
{
	if(data.length == 0) return
//	Pm.Echo('Data: '+  data)
	if(data == ':ok' )
	{
		Pm.Echo( ' Particle stream started')
		return
	}

	if( data.indexOf( 'event' ) >= 0 )
	{
		event = data.substring( data.indexOf(':') + 2)
		return
	}

	if( !data.indexOf('}') ) // no event or bad format?
	{
		Pm.Echo('Particle: Unexpected data: ' + data)
		return
	}

	data = data.substr( data.indexOf('{') ) // remove the leading "data"

	Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
			data.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + data + ')')

	switch(event)
	{
		case 'spark/status':
			online = (Json.data == 'online') ? true:false
			Pm.SparkRemote( 'CloudStatus', online)
			break
		case 'spark/flash/status':
			Pm.Echo('Flash Status ' + Json.data) // flash status
			break
		case 'spark/cc3000-patch-version': // this posts every time the device comes online
			cc3000version = Json.data
			break
		case 'stateChg':	// Mine.  mode/state/fan change
			d = isoDateToJsDate( Json.published_at )
//			Pm.Echo('pub at ' + d)

			str = Json.data
			Json = !(/[^,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t]/.test(
				str.replace(/"(\\.|[^"\\])*"/g, ''))) && eval('(' + str + ')')
			Pm.Echo('Mode = ' + Json.Mode + ' State = ' + Json.State + ' Fan = ' + Json.Fan)

			LogHVAC( Math.floor( d.getTime() / 1000), Json.State, Json.Fan )

			Pm.SparkRemote('UPDATE', Json.State, Json.Fan)
			OnTimer()  // read the rest of the data
			break
		case 'status':
			Pm.Echo('PL Status: ' + Json.data)
			break
		default:
			Pm.Echo('PL Unknown event: ' + event)
			break	
	}
}

function LogHVAC( uxt, state, fan )
{
	fso = new ActiveXObject( 'Scripting.FileSystemObject' )

	tf = fso.OpenTextFile( 'hvacdata.log', 8, true)
	tf.WriteLine( uxt + ',' + state + ',' + fan )
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
