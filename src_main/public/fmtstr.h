//========= Copyright (c) 1996-2002, Valve LLC, All rights reserved. ==========
//
// Purpose: A simple class for performing safe and in-expression sprintf-style
//			string formatting
//
// $NoKeywords: $
//=============================================================================

#ifndef FMTSTR_H
#define FMTSTR_H

#include <stdarg.h>
#include <stdio.h>

#if defined( _WIN32 )
#pragma once
#endif

//=============================================================================

#if defined(_MSC_VER) && _MSC_VER >= 1900 && defined(_M_IX86)
#define FMTSTR_VA_START(args, formatAddress) \
    ((args) = (va_list)((formatAddress) + 1))
#else
#define FMTSTR_VA_START(args, formatAddress) \
    va_start(args, (*(formatAddress)))
#endif

// using macro to be compatable with GCC
#define FmtStrVSNPrintf( szBuf, nBufSize, ppszFormat ) \
	do \
	{ \
		int     result; \
		va_list arg_ptr; \
	\
		FMTSTR_VA_START(arg_ptr, ppszFormat); \
		result = Q_vsnprintf((szBuf), (nBufSize)-1, (*(ppszFormat)), arg_ptr); \
		va_end(arg_ptr); \
	\
		(szBuf)[(nBufSize)-1] = 0; \
	} \
	while (0)

//-----------------------------------------------------------------------------
//
// Purpose: String formatter with specified size
//

template <int SIZE_BUF>
class CFmtStrN
{
public:
	CFmtStrN()									{ m_szBuf[0] = 0; }
	
	// Standard C formatting
	CFmtStrN(const char *pszFormat, ...)		{ FmtStrVSNPrintf(m_szBuf, SIZE_BUF, &pszFormat); }

	// Use this for pass-through formatting
	CFmtStrN(const char ** ppszFormat, ...)		{ FmtStrVSNPrintf(m_szBuf, SIZE_BUF, ppszFormat); }

	// Explicit reformat
	void sprintf(const char *pszFormat, ...)	{ FmtStrVSNPrintf(m_szBuf, SIZE_BUF, &pszFormat); }

	// Use this for pass-through formatting
	void VSprintf(const char **ppszFormat, ...)	{ FmtStrVSNPrintf(m_szBuf, SIZE_BUF, ppszFormat); }

	// Use for access
	operator const char *() const				{ return m_szBuf; }
	char *Access()								{ return m_szBuf; }

	void Clear()								{ m_szBuf[0] = 0; }

private:
	char m_szBuf[SIZE_BUF];
};

//-----------------------------------------------------------------------------
//
// Purpose: Default-sized string formatter
//

#define FMTSTR_STD_LEN 256

typedef CFmtStrN<FMTSTR_STD_LEN> CFmtStr;

//=============================================================================

#endif // FMTSTR_H
