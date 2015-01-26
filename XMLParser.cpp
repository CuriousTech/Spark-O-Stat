#include "application.h"
#include "XMLParser.h"
#include <time.h>

#define TIMEOUT 10000 // Allow maximum 10s between data packets.

XMLParser::XMLParser(char *pBuffer, int16_t bufSize)
{
	m_pBuffer = pBuffer;
	m_pEnd = m_pBuffer + bufSize - 1;
	m_pPtr = m_pEnd;    // Future: change to beginning for serialized
	m_pIn = m_pBuffer;
	m_pValue = NULL;
	
	m_pStatus = "";
    m_debug = 0;
}

void XMLParser::sendHeader(const char *pHeaderName, const char *pHeaderValue)
{
	m_client.print(pHeaderName);
	m_client.print(": ");
	m_client.println(pHeaderValue);
}

void XMLParser::sendHeader(const char *pHeaderName, int nHeaderValue)
{
	m_client.print(pHeaderName);
	m_client.print(": ");
	m_client.println(nHeaderValue);
}

bool XMLParser::begin(const char *pHost, const char *pPath, http_header_t headers[])
{
	m_pValue = NULL;
	m_bEOF = true;

	Serial.println("Connecting...");
  if( !m_client.connect(pHost, 80) )
    return false;
        
	Serial.println("Connected");

	// Send initial headers (only HTTP 1.0 is supported for now).
	m_client.print("GET");
	m_client.print(" ");
	m_client.print(pPath);
	m_client.println(" HTTP/1.1");

	// Send General and Request Headers.
	sendHeader("Host", pHost);
	sendHeader("Connection", "close");
	
    if(headers)
    {
        int i = 0;
        while (headers[i].header)
        {
            if (headers[i].value)
            {
                sendHeader(headers[i].header, headers[i].value);
            } else
            {
                m_client.println(headers[i].header);
            }
            i++;
        }
    }

	// Empty line to finish headers
	m_client.println();
	m_client.flush();

	m_bEOF = false;
	return true;
}

// Data read started
bool XMLParser::Ready()
{
    return (m_client.connected() && m_client.available());
}

// Serialized reader
bool XMLParser::service()
{
    if( !m_client.connected() )
        return false;

    char c = 0;
    
    while(m_pIn < m_pEnd && m_client.available())
    {
        *m_pIn++ = c = m_client.read();
    }
    if(c == 0x0A && (m_pIn - m_pBuffer) > 8 )
    {
        if( tagCompare(m_pIn - 8, "</dwml>") )    // end of file
        {
            m_client.stop();
            return true;
        }
    }

    return (m_pIn == m_pEnd) ? true : false;
}

// Serialzied test
void XMLParser::dump()
{
    while(m_pPtr < m_pEnd)
    {
        Serial.print(*m_pPtr);
        m_pPtr++;
    }
    m_pPtr = m_pBuffer;
    m_pIn = m_pBuffer;
}

void XMLParser::end()
{
	m_client.stop();
}

// Blocking reader
void XMLParser::fillBuffer()
{
	if(!m_client.connected())
	{
		if(m_pPtr == 0)
		    m_bEOF = true;
		return;
	}

	uint16_t bytesToRead = (m_pPtr - m_pBuffer);	// used bytes count

	if(m_pEnd - m_pPtr)	// remove all used bytes
	{
		memcpy(m_pBuffer, m_pPtr, m_pEnd - m_pPtr);
	}

	char *p = m_pEnd - bytesToRead;					// ptr to next unread byte

	unsigned long lastRead = millis();

	uint16_t count = 0;
	while(count < bytesToRead)
	{
    	if( !m_client.available() )
        {
    		if ( (millis() - lastRead) > TIMEOUT)
    		{
          Serial.println("fillBuffer timeout");
    			break;
    		}

            if(p - m_pBuffer > 8 )
            {
                if( tagCompare(p-8, "</dwml>") )    // end of file
                {
                    break;
                }
            }

//            Serial.print("waiting ms = ");
//            Serial.println( (millis() - lastRead) );
            delay(500);
        }
        else
        {
      		char c = m_client.read();

//      	Serial.print(c);
//    		if(c == -1)
//    		{
//                Serial.println("end char");
//    		}
    	    lastRead = millis();
      		*p++ = c;
    	  	count++;
    	  	m_debug ++;
        }
	}
	*p = 0;		// null term after last avail byte

  Serial.println();
	Serial.print("completed bytes: ");
	Serial.println(count);

	m_pPtr = m_pBuffer;

	if(count != bytesToRead)
	{
        Serial.print( "!bytesRead " );
        Serial.print(count);
        Serial.print( " of " );
        Serial.println(bytesToRead);
        Serial.print( " total=" );
        Serial.println(m_debug);
		m_client.stop();
	}
}

bool XMLParser::tagCompare(char *p1, const char *p2) // compare at lenngth of p1
{
	while(*p2)
	{
		if(*p1 == 0) return false;
		if(*p1++ != *p2++) return false;
	}
	return (*p2 == 0 && (*p1 == ' ' || *p1 == '>' || *p1 == '=' || *p1 == '"' || *p1 == 0x0A) ); // Fix: 0x0A dwml end tag hack
}

void XMLParser::IncPtr()
{
	if(++m_pPtr >= m_pEnd) fillBuffer();
}

void XMLParser::tagStart()
{
	while(!m_bEOF && *m_pPtr && *m_pPtr != '<') // find a tag
	{
		if(++m_pPtr >= m_pEnd) fillBuffer();
	}
}

void XMLParser::tagEnd()
{
	while(!m_bEOF && *m_pPtr && *m_pPtr != '>') // find a tag
	{
		if(++m_pPtr >= m_pEnd) fillBuffer();
	}
}

// Find a tag
bool XMLParser::findTag(const char *pTagName, const char *pAttr, const char *pValue)
{
	m_pTagName = pTagName;
	int len = strlen(pTagName);

	m_pValue = NULL;
	fillBuffer();

	while(!m_bEOF)
	{
		tagStart();				// find start of a tag

		if(*m_pPtr != '<')
		{
//    m_pStatus = "tag error";
			return false;		// last one
		}

		m_pPtr++;
		if(m_pPtr + len >= m_pEnd) fillBuffer();	// fill up enough to compare

		if(tagCompare(m_pPtr, pTagName))
		{
			m_pPtr += len;
			fillBuffer(); // fill it up

			while(!m_bEOF && *m_pPtr && *m_pPtr != '>') // end tag
			{
				if(pAttr)
				{
					if(tagCompare(m_pPtr, pAttr))
					{
						m_pPtr += strlen(pAttr) + 2;

						bool bFound = tagCompare(m_pPtr, pValue);

						tagEnd();
						m_pPtr++;
						if(bFound) return true;
					}
				}

				IncPtr();
			}

			m_pPtr++;	// after tag
			fillBuffer(); // get it full
			if(!pAttr) return true;
		}
	}
	return false;
}

// Get next tag data
char *XMLParser::nextTag()
{
	fillBuffer();	// get buffer full

	tagStart();		// Find start of tag
	IncPtr();
	if(m_bEOF) return NULL;

	if(*m_pPtr == '/')	// an end tag
	{ 
		IncPtr();
		if(tagCompare(m_pPtr, m_pTagName)) // end of value list
    {
			return NULL;
    }
	}

	tagEnd();			// end of start tag
	IncPtr();
	if(m_bEOF) return NULL;

	char *ptr = m_pPtr;	// data

	tagStart();
	if(m_bEOF) return NULL;

	*m_pPtr++ = 0;		// null term data

	tagEnd();			// skip past end of end tag
	IncPtr();

	return ptr;
}
