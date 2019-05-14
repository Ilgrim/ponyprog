//=========================================================================//
//                                                                         //
//  PonyProg - Serial Device Programmer                                    //
//                                                                         //
//  Copyright (C) 1997-2019   Claudio Lanconelli                           //
//                                                                         //
//  http://ponyprog.sourceforge.net                                        //
//                                                                         //
//-------------------------------------------------------------------------//
//                                                                         //
// This program is free software; you can redistribute it and/or           //
// modify it under the terms of the GNU  General Public License            //
// as published by the Free Software Foundation; either version2 of        //
// the License, or (at your option) any later version.                     //
//                                                                         //
// This program is distributed in the hope that it will be useful,         //
// but WITHOUT ANY WARRANTY; without even the implied warranty of          //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       //
// General Public License for more details.                                //
//                                                                         //
// You should have received a copy of the GNU  General Public License      //
// along with this program (see LICENSE);     if not, write to the         //
// Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. //
//                                                                         //
//=========================================================================//

// #include <stdio.h>
#include <QApplication>
#include <QtCore>
#include <QDebug>
#include <QString>

#include "e2profil.h"
#include "rs232int.h"
#include "errcode.h"

#ifdef Q_OS_LINUX
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#define INVALID_HANDLE_VALUE    -1
#endif

SerialInterface::SerialInterface()
{
	qDebug() << "SerialInterface::SerialInterface()";

	//      profile = prof;

	//COM default settings
	actual_speed = 9600;
	actual_parity = 'N';
	actual_bits = 8;
	actual_stops = 1;
	actual_flowcontrol = 0; //No flow control by default

	//NO timeouts by default
	read_total_timeout = 0;
	read_interval_timeout = 0;

	wait_endTX_mode = false;


#ifdef Q_OS_WIN32
	hCom = INVALID_HANDLE_VALUE;
#elif defined(Q_OS_LINUX)
	fd = INVALID_HANDLE_VALUE;
#endif

	//By default com_no == 0, so don't open any serial port if the constructor is called with zero paramameters
//	OpenSerial(com_no);

	qDebug() << "SerialInterface::SerialInterface() O";
}

SerialInterface::~SerialInterface()
{
	qDebug() << "SerialInterface::~SerialInterface()";

	CloseSerial();
}


#ifdef Q_OS_LINUX
static int fd_clear_flag(int fd, int flags);
#endif

/**
 * @brief
 *
 */
int SerialInterface::OpenSerial(int no)
{
	int ret_val = E2ERR_OPENFAILED;
	QString devname;

	if (no >= 0 && no < 32)
	{
#ifdef Q_OS_WIN32
		no++;           //linux call ttyS0 --> COM1, ttyS1 --> COM2, etc..
#endif
		devname = E2Profile::GetCOMDevName() + QString::number(no);

		ret_val = OpenSerial(devname);
	}

	return ret_val;
}
#if 0
/**
 * @brief
 *
 */
int SerialInterface::OpenUSB(uint16_t vid, uint16_t pid)
{
	int ret_val = E2ERR_OPENFAILED;

	if (vid > 0 && pid > 0)
	{
		CloseSerial();

		GetUSBInterface()->Open(vid, pid); // or depended from selected

		if (SetSerialTimeouts() != OK)
		{
			qDebug() << "SerialInterface::OpenUSB SetSerialTimeouts() failed";
			CloseSerial();
		}
		else if (SetSerialParams() != OK)
		{
			qDebug() << "SerialInterface::OpenUSB SetSerialParams() failed";
			CloseSerial();
		}
		else
		{
			SetSerialTimeouts();
			SetSerialParams();

			ret_val = OK;
		}
	}
	return ret_val;
}
#endif
/**
 * @brief
 *
 */
int SerialInterface::OpenSerial(QString devname)
{
	qDebug() << "SerialInterface::OpenSerial(" << devname << ") I";

	int ret_val = E2ERR_OPENFAILED;

	m_devname = devname;

#ifdef Q_OS_WIN32
	hCom = CreateFile((LPCWSTR)m_devname.utf16(),
					  GENERIC_READ | GENERIC_WRITE,
					  0,              /* comm devices must be opened w/exclusive-access */
					  NULL,   /* no security attrs */
					  OPEN_EXISTING, /* comm devices must use OPEN_EXISTING */
					  0,              /* not overlapped I/O */
					  NULL    /* hTemplate must be NULL for comm devices */
					 );

	if (hCom != INVALID_HANDLE_VALUE)
	{
		GetCommState(hCom, &old_dcb);
		GetCommTimeouts(hCom, &old_timeout);
		GetCommMask(hCom, &old_mask);

		if (wait_endTX_mode)
		{
			SetCommMask(hCom, EV_TXEMPTY);
		}
		else
		{
			SetCommMask(hCom, 0);
		}

		SetSerialTimeouts();
		SetSerialParams();

		ret_val = OK;
	}
#elif defined(Q_OS_LINUX)

	fd = INVALID_HANDLE_VALUE;

	qDebug() << "SerialInterface::OpenSerial() now open the device " << m_devname;

	fd = open(m_devname.toLatin1().constData(), O_RDWR | O_NONBLOCK | O_EXCL);

	qDebug() << "SerialInterface::OpenSerial open result = " << fd;

	if (fd < 0)
	{
		qDebug() << "SerialInterface::OpenSerial can't open the device " << devname;
		return ret_val;
	}

	// Check for the needed IOCTLS
#if defined(TIOCSBRK) && defined(TIOCCBRK) //check if available for compilation

	// Check if available during runtime
	if ((ioctl(fd, TIOCSBRK, 0) == -1) || (ioctl(fd, TIOCCBRK, 0) == -1))
	{
		qDebug() << "SerialInterface::OpenPort IOCTL not available";
		close(fd);
		fd = INVALID_HANDLE_VALUE;
		return ret_val;
	}
#else
	close(fd);
	fd = INVALID_HANDLE_VALUE;
	return ret_val;
#endif  /*TIOCSBRK*/

	/* open sets RTS and DTR, reset it */
#if defined(TIOCMGET) && defined(TIOCMSET) //check if available for compilation
	int flags;

	if (ioctl(fd, TIOCMGET, &flags) == -1)
	{
		qDebug() << "SerialInterface::OpenPort IOCTL not available";
		close(fd);
		fd = INVALID_HANDLE_VALUE;
		return ret_val;
	}
	else
	{
		flags &= ~(TIOCM_RTS | TIOCM_DTR);

		if (ioctl(fd, TIOCMSET, &flags) == -1)
		{
			qDebug() << "SerialInterface::OpenPort IOCTL not available";
			close(fd);
			fd = INVALID_HANDLE_VALUE;
			return ret_val;
		}
	}
#endif /*TIOCMGET */

	qDebug() << "SerialInterface::OpenPort GETATTR";

	if (tcgetattr(fd, &old_termios) == -1)
	{
		qDebug() << "SerialInterface::OpenPort GETATTR failed";

		close(fd);
		fd = INVALID_HANDLE_VALUE;
		return ret_val;
	}

	qDebug() << "SerialInterface::OpenPort SetTimeouts && Params";

	if (SetSerialTimeouts() != OK)
	{
		qDebug() << "SerialInterface::OpenPort SetSerialTimeouts() failed";
		close(fd);
		fd = INVALID_HANDLE_VALUE;
	}
	else if (SetSerialParams() != OK)
	{
		qDebug() << "SerialInterface::OpenPort SetSerialParams() failed";
		close(fd);
		fd = INVALID_HANDLE_VALUE;
	}
	else
	{
		fd_clear_flag(fd, O_NONBLOCK);          //Restore to blocking mode
		ret_val = OK;
	}
#endif  /*Q_OS_LINUX*/

	qDebug() << "SerialInterface::OpenSerial() = " << ret_val << " O";

	return ret_val;
}

/**
 * @brief
 *
 */
void SerialInterface::CloseSerial()
{
	qDebug() << "SerialInterface::CloseSerial()";

	if (usb_pid > 0 || usb_vid > 0)
	{
		//TODO disconnect? when flashing runs?
		// close the usb
		GetUSBInterface()->Close();
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		//              SetCommState(hCom, &old_dcb);           //This can raise the RTS line, so invalidating the PowerOff
		SetCommTimeouts(hCom, &old_timeout);
		SetCommMask(hCom, old_mask);
		PurgeComm(hCom, PURGE_TXCLEAR | PURGE_RXCLEAR);

		CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		//	tcsetattr(fd, TCSAFLUSH, &old_termios);         //This can raise the RTS line, so invalidating the PowerOff
		close(fd);
		fd = INVALID_HANDLE_VALUE;
	}

#endif
}

/**
 * @brief
 *
 */
int SerialInterface::SetSerialBreak(int state)
{
	int result = E2ERR_OPENFAILED;

	if (usb_vid > 0 && usb_pid > 0)
	{
		result = GetUSBInterface()->SetBreakControl(state);

		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (state)
		{
			SetCommBreak(hCom);
		}
		else
		{
			ClearCommBreak(hCom);
		}

		result = OK;
	}

#elif defined(Q_OS_LINUX)

#if defined(TIOCSBRK) && defined(TIOCCBRK) //check if available for compilation 

	if (state)
	{
		result = ioctl(fd, TIOCSBRK, 0);
	}
	else
	{
		result = ioctl(fd, TIOCCBRK, 0);
	}

#else
	qDebug() << "SerialInterface::SetSerialBreak Can't get IOCTL";
#endif

#endif

	return result;
}

/**
void SerialInterface::SetSerialEventMask(long mask)
{
#ifdef Q_OS_WIN32
        if (hCom != INVALID_HANDLE_VALUE )
                SetCommMask(hCom, mask);
#endif
}
**/

/**
 * @brief
 *
 */
void SerialInterface::SerialFlushRx()
{
	if (usb_vid > 0 && usb_pid > 0)
	{
		// TODO flushRx
		// this function is not in use
	}
#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		PurgeComm(hCom, PURGE_RXCLEAR);
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		tcflush(fd, TCIFLUSH);
	}

#endif
}

void SerialInterface::SerialFlushTx()
{
	if (usb_vid > 0 && usb_pid > 0)
	{
		//TODO flushTx
		// this function is not in use
	}
#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		PurgeComm(hCom, PURGE_TXCLEAR);
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		tcflush(fd, TCOFLUSH);
	}

#endif
}

void SerialInterface::WaitForTxEmpty()
{
	if (usb_vid > 0 && usb_pid > 0)
	{
		// TODO to check this
		uint32_t completed;
		do
		{
			completed = GetUSBInterface()->GetStatusTx();
			QApplication::processEvents();
		}
		while (!completed);
	}

#ifdef Q_OS_WIN32
	DWORD evento;

	if (hCom != INVALID_HANDLE_VALUE)
	{
		do
		{
			WaitCommEvent(hCom, &evento, NULL);
		}
		while (!(evento & EV_TXEMPTY));
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		tcdrain(fd);
	}

#endif
}

/**
 * @brief this function is not in using
 */
long SerialInterface::ReadSerial(uint8_t *buffer, long len)
{
	long retval = E2ERR_OPENFAILED;

	if (usb_vid > 0 && usb_pid > 0)
	{
		// TODO to implement I2C, SPI
		long nread, nleft;
		uint8_t *ptr;

		nleft = len;
		ptr = buffer;

		while (nleft > 0)
		{
			// TODO ???
			nread = GetUSBInterface()->Read(ptr, nleft);

			if (nread < 0)
			{
				nleft = -1;
				break;  //Error
			}

			nleft -= nread;
			ptr   += nread;

			QApplication::processEvents();
		}

		if (nleft >= 0)
		{
			retval = (len - nleft);
		}
		return retval;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		DWORD nread;

		if (ReadFile(hCom, buffer, len, &nread, NULL))
		{
			retval = nread;
		}
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		long nread, nleft;
		uint8_t *ptr;

		nleft = len;
		ptr = buffer;

		/* Wait up to N seconds. */
		struct timeval tv;

		tv.tv_sec = read_total_timeout / 1000;
		tv.tv_usec = (read_total_timeout % 1000) * 1000;

		while (nleft > 0)
		{
			fd_set rfds;
			int rval;

			/* Watch file fd to see when it has input. */
			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);

			rval = select(fd + 1, &rfds, NULL, NULL, &tv);

			if (rval < 0)   //Error
			{
				nleft = -1;
				break;
			}
			else if (rval == 0)     //Timeout
			{
				nleft = -1;
				retval = E2P_TIMEOUT;
				break;
			}
			else                    //Ok
			{
				nread = read(fd, ptr, nleft);

				if (nread < 0)
				{
					nleft = -1;
					break;  //Error
				}
			}

			nleft -= nread;
			ptr   += nread;
		}

		if (nleft >= 0)
		{
			retval = (len - nleft);
		}
	}

#endif

	return retval;
}

/**
 * @brief this function is not in using
 */
long SerialInterface::WriteSerial(uint8_t *buffer, long len)
{
	long retval = E2ERR_OPENFAILED;

	if (usb_vid > 0 && usb_pid > 0)
	{
		// TODO to implement I2C, SPI
		long nleft;
		uint8_t *ptr;

		ptr = buffer;
		nleft = len;

		while (nleft > 0)
		{
			// TODO ???
			long nwritten = GetUSBInterface()->Write(ptr, nleft);

			if (nwritten <= 0)
			{
				return retval;        //return error
			}

			nleft -= nwritten;
			ptr   += nwritten;

			QApplication::processEvents();
		}

		retval = len;

		return retval;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		DWORD nwrite;

		if (WriteFile(hCom, buffer, len, &nwrite, NULL))
		{
			retval = nwrite;
		}
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		long nleft;
		uint8_t *ptr;

		ptr = buffer;
		nleft = len;

		while (nleft > 0)
		{
			long nwritten = write(fd, ptr, nleft);

			if (nwritten <= 0)
			{
				return retval;        //return error
			}

			nleft -= nwritten;
			ptr   += nwritten;
		}

		retval = len;
	}

#endif

	if (wait_endTX_mode)
	{
		WaitForTxEmpty();
	}

	return retval;
}

// EK: now it works with settings from e2dlg popup
int SerialInterface::SetSerialParams(long speed, int bits, int parity, int stops, int flow_control)
{
	int result = E2ERR_OPENFAILED;

	// read settings

	if (speed == -1)
	{
		speed = E2Profile::GetBaudrate().toInt();
	}

	if (parity == -1)
	{
		// only first char: N, O, E
		parity = E2Profile::GetParity().at(0).toLatin1();
	}

	if (bits == -1)
	{
		// convert from char to number
		bits = E2Profile::GetDatabits().at(0).toLatin1() - 0x30;
	}

	if (stops == -1)
	{
		// convert from char to number
		stops = E2Profile::GetStopbits().at(0).toLatin1() - 0x30;
	}

	if (flow_control == -1)
	{
		char c = E2Profile::GetFlowcontrol().at(0).toLatin1();
		if (c == 'N')
		{
			// none flow control
			flow_control = 0;
		}

		if (c == 'R')
		{
			// RTS/CTS
			flow_control = 1;
		}

		if (c == 'X')
		{
			// XON/XOFF
			flow_control = 2;
		}

		if (c == 'D')
		{
			// DTR/DSR
			flow_control = 3;
		}
	}

	// end read of settings

	if (usb_vid > 0 && usb_pid > 0)
	{
		GetUSBInterface()->SetChipMode(USB_MODE_UART);

		if (speed >= 300 && speed <= 230400)
		{
			actual_speed = speed;
		}

		if (bits >= 1 && bits <= 16)
		{
			actual_bits = bits;
		}
		GetUSBInterface()->SetBits(bits);

		if (parity == 'N' || parity == 'E' || parity == 'O')
		{
			actual_parity = parity;
		}
		GetUSBInterface()->SetParity(parity);

		if (stops >= 1 && stops <= 2)
		{
			actual_stops = stops;
		}
		GetUSBInterface()->SetStops(stops);

		if (flow_control >= 0 && flow_control <= 2)
		{
			actual_flowcontrol = flow_control;
		}
		GetUSBInterface()->SetFlowControl(flow_control);

		result = GetUSBInterface()->SetBaudRate(speed);
		if (result)
		{
			// TODO message
		}

		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (speed >= 300 && speed <= 115200)
		{
			actual_speed = speed;
		}

		if (bits >= 1 && bits <= 16)
		{
			actual_bits = bits;
		}

		if (parity == 'N' || parity == 'E' || parity == 'O')
		{
			actual_parity = parity;
		}

		if (stops >= 1 && stops <= 2)
		{
			actual_stops = stops;
		}

		if (flow_control >= 0 && flow_control <= 2)
		{
			actual_flowcontrol = flow_control;
		}

		QString dcb_str;
		DCB com_dcb;

		if (GetCommState(hCom, &com_dcb))
		{
			dcb_str.sprintf("baud=%ld parity=%c data=%d stop=%d", actual_speed, actual_parity, actual_bits, actual_stops);
// 			dcb_str[255] = '\0';

			if (BuildCommDCB((LPCWSTR)dcb_str.utf16(), &com_dcb))
			{
				if (actual_flowcontrol == 0)
				{
					com_dcb.fDtrControl = DTR_CONTROL_DISABLE;
					com_dcb.fRtsControl = RTS_CONTROL_DISABLE;
				}

				if (SetCommState(hCom, &com_dcb))
				{
					result = OK;
				}
				else
				{
					result = GetLastError();
				}

				PurgeComm(hCom, PURGE_TXCLEAR | PURGE_RXCLEAR);
			}
		}
	}

#elif defined(Q_OS_LINUX)

	if (fd != INVALID_HANDLE_VALUE)
	{
		if (speed >= 300 && speed <= 115200)
		{
			actual_speed = speed;
		}

		if (bits >= 1 && bits <= 16)
		{
			actual_bits = bits;
		}

		if (parity == 'N' || parity == 'E' || parity == 'O')
		{
			actual_parity = parity;
		}

		if (stops >= 1 && stops <= 2)
		{
			actual_stops = stops;
		}

		if (flow_control >= 0 && flow_control <= 2)
		{
			actual_flowcontrol = flow_control;
		}

		struct termios termios;

		if (tcgetattr(fd, &termios) != 0)
		{
			return result;
		}

		cfmakeraw(&termios);
		termios.c_cflag |= CLOCAL;              //Disable modem status line check

		//Flow control
		if (actual_flowcontrol == 0)
		{
			termios.c_cflag &= ~CRTSCTS;    //Disable hardware flow control
			termios.c_iflag &= ~(IXON | IXOFF);     //Disable software flow control
		}
		else if (actual_flowcontrol == 1)
		{
			termios.c_cflag |= CRTSCTS;
			termios.c_iflag &= ~(IXON | IXOFF);
		}
		else
		{
			termios.c_cflag &= ~CRTSCTS;
			termios.c_iflag |= (IXON | IXOFF);
		}

		//Set size of bits
		termios.c_cflag &= ~CSIZE;

		if (actual_bits <= 5)
		{
			termios.c_cflag |= CS5;
		}
		else if (actual_bits == 6)
		{
			termios.c_cflag |= CS6;
		}
		else if (actual_bits == 7)
		{
			termios.c_cflag |= CS7;
		}
		else
		{
			termios.c_cflag |= CS8;
		}

		//Set stop bits
		if (actual_stops == 2)
		{
			termios.c_cflag |= CSTOPB;
		}
		else
		{
			termios.c_cflag &= ~CSTOPB;
		}

		//Set parity bit
		if (actual_parity == 'N')
		{
			termios.c_cflag &= ~PARENB;
		}
		else if (actual_parity == 'E')
		{
			termios.c_cflag |= PARENB;
			termios.c_cflag &= ~PARODD;
		}
		else
		{
			//'O'
			termios.c_cflag |= (PARENB | PARODD);
		}

		//Set speed
		speed_t baudrate;

		switch (speed)
		{
		case 300:
			baudrate = B300;
			break;

		case 600:
			baudrate = B600;
			break;

		case 1200:
			baudrate = B1200;
			break;

		case 2400:
			baudrate = B2400;
			break;

		case 4800:
			baudrate = B4800;
			break;

		case 9600:
			baudrate = B9600;
			break;

		case 19200:
			baudrate = B19200;
			break;

		case 38400:
			baudrate = B38400;
			break;

		case 57600:
			baudrate = B57600;
			break;

		case 115200:
			baudrate = B115200;
			break;

		case 230400:
			baudrate = B230400;
			break;

		default:
			baudrate = B9600;
			break;
		}

		cfsetispeed(&termios, baudrate);
		cfsetospeed(&termios, baudrate);

		termios.c_cc[VMIN] = 1;
		termios.c_cc[VTIME] = 0;

		if (tcsetattr(fd, TCSANOW, &termios) == 0)
		{
			result = OK;
		}
	}

#endif

	return result;
}

//At the moment the while_read (interval timeout) is not used with Linux
int SerialInterface::SetSerialTimeouts(long init_read, long while_read)
{
	long result = E2ERR_OPENFAILED;

	if (while_read >= 0)
	{
		read_interval_timeout = while_read;
	}

	if (init_read >= 0)
	{
		read_total_timeout = init_read;
	}

	if (usb_pid > 0 || usb_vid > 0)
	{
		// TODO

		return OK;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		COMMTIMEOUTS new_timeout;

		//              new_timeout = old_timeout;

		/*
		 * Set to asynchronous mode: the read operation is to
		 * return immediately with the characters that have
		 * already been received, even if no characters have
		 * been received.
		 *
		 * ReadIntervalTimeout = MAXDWORD;
		 * ReadTotalTimeoutMultiplier = 0;
		 * ReadTotalTimeoutConstant = 0;
		 */

		/*
		 * Windows 95: Set to UNIX read() syscall behaviour
		 *
		 * ReadIntervalTimeout = MAXDWORD;
		 * readTotalTimeoutMultiplier = MAXDWORD;
		 * ReadTotalTimeoutConstant = X (X > 0 && X < MAXDWORD)
		 */

		new_timeout.ReadIntervalTimeout = read_interval_timeout;
		new_timeout.ReadTotalTimeoutMultiplier = 0;
		new_timeout.ReadTotalTimeoutConstant = read_total_timeout;

		//Disable write timeouts
		new_timeout.WriteTotalTimeoutMultiplier = 0;
		new_timeout.WriteTotalTimeoutConstant = 0;

		if (SetCommTimeouts(hCom, &new_timeout))
		{
			result = OK;
		}
	}

#elif defined(Q_OS_LINUX)
	result = OK;
#endif

	return result;
}

int SerialInterface::SetSerialDTR(int dtr)
{
	int result = E2ERR_OPENFAILED;

	if (usb_pid > 0 || usb_vid > 0)
	{
		result = GetUSBInterface()->SetDTR(dtr);

		if (result)
		{
			// TODO message
		}
		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (EscapeCommFunction(hCom, dtr ? SETDTR : CLRDTR))
		{
			result = OK;
		}
	}

#elif defined(Q_OS_LINUX)
	int flags;

	ioctl(fd, TIOCMGET, &flags);

	if (dtr)
	{
		flags |= TIOCM_DTR;
	}
	else
	{
		flags &= ~TIOCM_DTR;
	}

	result = ioctl(fd, TIOCMSET, &flags);
#endif

	return result;
}

int SerialInterface::SetSerialRTS(int rts)
{
	int result = E2ERR_OPENFAILED;

	if (usb_pid > 0 || usb_vid > 0)
	{
		result = GetUSBInterface()->SetRTS(rts);
		if (result)
		{
			//TODO message
		}
		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (EscapeCommFunction(hCom, rts ? SETRTS : CLRRTS))
		{
			result = OK;
		}
	}

#elif defined(Q_OS_LINUX)
	int flags;

	ioctl(fd, TIOCMGET, &flags);

	if (rts)
	{
		flags |= TIOCM_RTS;
	}
	else
	{
		flags &= ~TIOCM_RTS;
	}

	result = ioctl(fd, TIOCMSET, &flags);
#endif

	return result;
}

int SerialInterface::SetSerialRTSDTR(int state)
{
	int result = E2ERR_OPENFAILED;

	if (usb_pid > 0 || usb_vid > 0)
	{
		result = GetUSBInterface()->SetRTSDTR(state);

		if (result)
		{
			//TODO message
		}
		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		if (state)
		{
			EscapeCommFunction(hCom, SETRTS);
			EscapeCommFunction(hCom, SETDTR);
		}
		else
		{
			EscapeCommFunction(hCom, CLRRTS);
			EscapeCommFunction(hCom, CLRDTR);
		}

		result = OK;
	}

#elif defined(Q_OS_LINUX)

	int flags;
	ioctl(fd, TIOCMGET, &flags);

	if (state)
	{
		flags |= (TIOCM_RTS | TIOCM_DTR);
	}
	else
	{
		flags &= ~(TIOCM_RTS | TIOCM_DTR);
	}

	result = ioctl(fd, TIOCMSET, &flags);

#endif

	return result;
}

int SerialInterface::GetSerialDSR()
{
	int result = E2ERR_OPENFAILED;

	if (usb_pid > 0 || usb_vid > 0)
	{
		return GetUSBInterface()->GetDSR();
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		DWORD status;

		if (GetCommModemStatus(hCom, &status))
		{
			result = (status & MS_DSR_ON);
		}
	}

#elif defined(Q_OS_LINUX)

	int flags;

	if (ioctl(fd, TIOCMGET, &flags) != -1)
	{
		result = (flags & TIOCM_DSR);
	}

#endif

	return result;
}

int SerialInterface::GetSerialCTS()
{
	int result = E2ERR_OPENFAILED;

	if (usb_pid > 0 || usb_vid > 0)
	{
		// TODO to check bits
		result = GetUSBInterface()->GetCTS();

		return result;
	}

#ifdef Q_OS_WIN32

	if (hCom != INVALID_HANDLE_VALUE)
	{
		DWORD status;

		if (GetCommModemStatus(hCom, &status))
		{
			result = (status & MS_CTS_ON);
		}
	}

#elif defined(Q_OS_LINUX)

	int flags;

	if (ioctl(fd, TIOCMGET, &flags) != -1)
	{
		result = (flags & TIOCM_CTS);
	}

#endif

	return result;
}

#ifdef Q_OS_LINUX
static int fd_clear_flag(int fd, int flags)
{
	int val;

	if ((val = fcntl(fd, F_GETFL, 0)) < 0)
	{
		return val;
	}

	val &= ~flags;

	if (fcntl(fd, F_SETFL, val) < 0)
	{
		return -1;
	}

	return 0;
}
#endif
