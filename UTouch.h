/*
  UTouch.h - Arduino/chipKit library support for Color TFT LCD Touch screens 
  Copyright (C)2010-2014 Henning Karlsen. All right reserved
  
  Basic functionality of this library are based on the demo-code provided by
  ITead studio. You can find the latest version of the library at
  http://www.henningkarlsen.com/electronics

  If you make any modifications or improvements to the code, I would appreciate
  that you share the code with me so that I might include it in the next release.
  I can be contacted through http://www.henningkarlsen.com/electronics/contact.php

  This library is free software; you can redistribute it and/or
  modify it under the terms of the CC BY-NC-SA 3.0 license.
  Please see the included documents for further information.

  Commercial use of this library requires you to buy a license that
  will allow commercial use. This includes using the library,
  modified or not, as a tool to sell products.

  The license applies to all part of the library including the 
  examples and tools supplied with the library.
*/

#ifndef UTouch_h
#define UTouch_h

#define UTOUCH_VERSION	124

#define PORTRAIT			0
#define LANDSCAPE			1

#define PREC_LOW			1
#define PREC_MEDIUM		2
#define PREC_HI				3
#define PREC_EXTREME	4

class UTouch
{
	public:
		int16_t	TP_X ,TP_Y;

		UTouch(uint8_t CS, uint8_t IRQ);

    void  InitTouch(uint16_t xSize, uint16_t ySize, uint16_t left, uint16_t right, uint16_t top, uint16_t bottom, byte orientation);
		void	read();
		bool	pressed();
		int16_t	getX();
		int16_t	getY();
    int16_t getCal(int n);
    void setCal(int n, int16_t v);
		void	setPrecision(byte precision);
private:
		uint8_t cs, irq;
		long	_default_orientation;
		byte	orient;
		byte	prec;
		long	disp_x_size, disp_y_size, default_orientation;
		long	touch_x_left, touch_x_right, touch_y_top, touch_y_bottom;
};

#endif
