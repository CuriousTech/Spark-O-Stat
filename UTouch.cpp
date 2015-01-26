/*
  UTouch.cpp - Arduino/chipKit library support for Color TFT LCD Touch screens 
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

#include "application.h"
#include "UTouch.h"

UTouch::UTouch(uint8_t CS, uint8_t IRQ)
{
	cs	= CS;
	irq	= IRQ;
}

void UTouch::InitTouch(uint16_t xSize, uint16_t ySize, uint16_t left, uint16_t right, uint16_t top, uint16_t bottom, byte orientation)
{
	orient					= orientation;
	touch_x_left			= left;
	touch_x_right			= right;
	touch_y_top				= top;
	touch_y_bottom			= bottom;
	disp_x_size				= xSize - 1;
	disp_y_size				= ySize - 1;
	prec					= 27;

  pinMode(cs, OUTPUT);
  pinMode(irq, INPUT_PULLUP);

  SPI.begin();

  read();
}

void UTouch::read()
{
	unsigned long tx = 0, temp_x = 0;
	unsigned long ty = 0, temp_y = 0;
	unsigned long minx = 99999, maxx = 0;
	unsigned long miny = 99999, maxy = 0;
	int datacount = 0;

  SPI.setClockDivider(SPI_CLOCK_DIV8);

	digitalWrite(cs, LOW);

	for(int i = 0; i < prec; i++)
	{
	    uint8_t a, b;

		SPI.transfer(0x90);
	
		a = SPI.transfer(0x00);
		b = SPI.transfer(0x00);
		temp_x = (a<<4) | (b>>4);

		SPI.transfer(0xD0);
		a = SPI.transfer(0x00);
		b = SPI.transfer(0x00);
		temp_y = (a<<4) | (b>>4);

		if ((temp_x>0) and (temp_x<4096) and (temp_y>0) and (temp_y<4096))
		{
			tx += temp_x;
			ty += temp_y;
			if(prec > 5)
			{
				if (temp_x < minx)	minx = temp_x;
				if (temp_x > maxx)	maxx = temp_x;
				if (temp_y < miny)	miny = temp_y;
				if (temp_y > maxy)	maxy = temp_y;
			}
			datacount++;
		}
	}

	if (prec>5)
	{
		tx = tx-(minx+maxx);
		ty = ty-(miny+maxy);
		datacount -= 2;
	}

	digitalWrite(cs, HIGH);

    SPI.setClockDivider(SPI_CLOCK_DIV2);

	if ((datacount==(prec-2)) or (datacount==PREC_LOW))
	{
		if (orient == _default_orientation)
		{
			TP_X=ty/datacount;
			TP_Y=tx/datacount;
		}
		else
		{
			TP_X=tx/datacount;
			TP_Y=ty/datacount;
		}
	}
	else
	{
		TP_X = -1;
		TP_Y = -1;
	}
}

bool UTouch::pressed()
{
	return (digitalRead(irq) == 0);
}

int16_t UTouch::getX()
{
	long c;

	if ((TP_X==-1) or (TP_Y==-1))
		return -1;
	if (orient == _default_orientation)
	{
		c = long(long(TP_X - touch_x_left) * (disp_x_size)) / long(touch_x_right - touch_x_left);
		if (c<0)
			c = 0;
		if (c>disp_x_size)
			c = disp_x_size;
	}
	else
	{
		if (_default_orientation == PORTRAIT)
			c = long(long(TP_X - touch_y_top) * (-disp_y_size)) / long(touch_y_bottom - touch_y_top) + long(disp_y_size);
		else
			c = long(long(TP_X - touch_y_top) * (disp_y_size)) / long(touch_y_bottom - touch_y_top);
		if (c<0)
			c = 0;
		if (c>disp_y_size)
			c = disp_y_size;
	}
	return c;
}

int16_t UTouch::getY()
{
	int c;

	if ((TP_X==-1) or (TP_Y==-1))
		return -1;
	if (orient == _default_orientation)
	{
		c = long(long(TP_Y - touch_y_top) * (disp_y_size)) / long(touch_y_bottom - touch_y_top);
		if (c<0)
			c = 0;
		if (c>disp_y_size)
			c = disp_y_size;
	}
	else
	{
		if (_default_orientation == PORTRAIT)
			c = long(long(TP_Y - touch_x_left) * (disp_x_size)) / long(touch_x_right - touch_x_left);
		else
			c = long(long(TP_Y - touch_x_left) * (-disp_x_size)) / long(touch_x_right - touch_x_left) + long(disp_x_size);
		if (c<0)
			c = 0;
		if (c>disp_x_size)
			c = disp_x_size;
	}
	return c;
}

int16_t UTouch::getCal(int n)
{
    switch(n)
    {
        case 0: return touch_x_left;
        case 1: return touch_x_right;
        case 2: return touch_y_top;
        case 3: return touch_y_bottom;
    }
    return 0;
}

void UTouch::setCal(int n, int16_t v)
{
    switch(n)
    {
        case 0:
            touch_x_left = v;
            break;
        case 1:
            touch_x_right = v;
            break;
        case 2:
            touch_y_top = v;
            break;
        case 3:
            touch_y_bottom = v;
            break;
    }
}

void UTouch::setPrecision(byte precision)
{
	switch (precision)
	{
		case PREC_LOW:
			prec=1;		// DO NOT CHANGE!
			break;
		case PREC_MEDIUM:
			prec=12;	// Iterations + 2
			break;
		case PREC_HI:
			prec=27;	// Iterations + 2
			break;
		case PREC_EXTREME:
			prec=102;	// Iterations + 2
			break;
		default:
			prec=12;	// Iterations + 2
			break;
	}
}
