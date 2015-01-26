#ifndef XMLPARSER_H
#define XMLPARSER_H

typedef struct
{
    const char* header;
    const char* value;
} http_header_t;

class XMLParser
{
public:
	XMLParser(char *pBuffer, int16_t bufSize);
	bool	begin(const char *pHost, const char *pPath, http_header_t headers[]);
  bool  Ready(void);
  bool  service(void);
  void  dump(void);
	void	end(void);
	bool	findTag(const char *pTagName, const char *pAttr, const char *pValue);
	char	*nextTag();

  const char *m_pStatus;
  int     m_debug;
private:
	void	fillBuffer();

  void  sendHeader(const char *pHeaderName, const char *pHeaderValue);
  void  sendHeader(const char *pHeaderName, int nHeaderValue);
	bool	tagCompare(char *p1, const char *p2);
	void	IncPtr(void);
	void	tagStart(void);
	void	tagEnd(void);

	TCPClient m_client;

	char	*m_pBuffer;
	char	*m_pPtr;
	char	*m_pEnd;
	char  *m_pIn;
	char	*m_pValue;
	const char *m_pTagName;
	bool	m_bEOF;
};

#endif // XMLPARSER_H
