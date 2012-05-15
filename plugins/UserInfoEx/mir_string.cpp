/*
UserinfoEx plugin for Miranda IM

Copyright:
� 2006-2010 DeathAxe, Yasnovidyashii, Merlin, K. Romanov, Kreol

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

===============================================================================

File name      : $HeadURL: https://userinfoex.googlecode.com/svn/trunk/mir_string.cpp $
Revision       : $Revision: 187 $
Last change on : $Date: 2010-09-08 16:05:54 +0400 (Ср, 08 сен 2010) $
Last change by : $Author: ing.u.horn $

===============================================================================
*/

#include "commonheaders.h"

char	*mir_strncpy(char *pszDest, const char *pszSrc, const size_t cchDest)
{
	if (!pszDest || !pszSrc || !cchDest)
		return NULL;
	pszDest = strncpy(pszDest, pszSrc, cchDest-1);
	pszDest[cchDest-1] = 0;
	return pszDest;
}

wchar_t	*mir_wcsncpy(wchar_t *pszDest, const wchar_t *pszSrc, const size_t cchDest)
{
	if (!pszDest || !pszSrc || !cchDest)
		return NULL;
	pszDest = wcsncpy(pszDest, pszSrc, cchDest-1);
	pszDest[cchDest-1] = 0;
	return pszDest;
}

char	*mir_strncat(char *pszDest, const char *pszSrc, const size_t cchDest)
{
	if (!pszDest || !pszSrc || !cchDest)
		return NULL;
	strncat(pszDest, pszSrc, cchDest-1);
	pszDest[cchDest-1] = 0;
	return pszDest;
}

wchar_t	*mir_wcsncat(wchar_t *pszDest, const wchar_t *pszSrc, const size_t cchDest)
{
	if (!pszDest || !pszSrc || !cchDest)
		return NULL;
	pszDest = wcsncat(pszDest, pszSrc, cchDest-1);
	pszDest[cchDest-1] = 0;
	return pszDest;
}

char	*mir_strncat_c(char *pszDest, const char cSrc) 
{
	size_t size = sizeof(char) * (strlen(pszDest) + 2);		//cSrc = 1 + 1 forNULL temination 
	if (!pszDest)
		pszDest = (char *) mir_alloc(size);
	else
		pszDest = (char *) mir_realloc(pszDest, size);
	pszDest[size-2] = cSrc;
	pszDest[size-1] = 0;
	return pszDest;
}

wchar_t	*mir_wcsncat_c(wchar_t *pwszDest, const wchar_t wcSrc) {
	size_t size = sizeof(wchar_t) * (wcslen(pwszDest) + 2);
	if (!pwszDest)
		pwszDest = (wchar_t *) mir_alloc(size);
	else
		pwszDest = (wchar_t *) mir_realloc(pwszDest, size);
	pwszDest[size-2] = wcSrc;
	pwszDest[size-1] = 0;
	return pwszDest;
}

char	*mir_strnerase(char *pszDest, size_t sizeFrom, size_t sizeTo) {
	char *pszReturn = NULL;
	size_t sizeNew, sizeLen = strlen(pszDest);
	if (sizeFrom >= 0 && sizeFrom < sizeLen && sizeTo >= 0 && sizeTo <= sizeLen && sizeFrom < sizeTo) {
		sizeNew = sizeLen - (sizeTo - sizeFrom);
		size_t sizeCopy = sizeNew - sizeFrom;
		pszReturn = (char *) mir_alloc(sizeNew + 1);
		memcpy(pszReturn, pszDest, sizeFrom);
		memcpy(pszReturn + sizeFrom, pszDest + sizeTo, sizeCopy);
		pszReturn[sizeNew] = 0;
	}

	pszDest = (char *) mir_realloc(pszDest, sizeNew + 1);
	pszDest = mir_strcpy(pszDest, pszReturn);
	mir_free(pszReturn);
	return pszDest;
}

size_t mir_vsnwprintf(wchar_t *pszDest, const size_t cchDest, const wchar_t *pszFormat, va_list& argList)
{
	size_t iRet, cchMax;

	if (!pszDest || !pszFormat || !*pszFormat)
		return -1;

	cchMax = cchDest - 1;
	iRet = _vsnwprintf(pszDest, cchMax, pszFormat, argList);
	if (iRet < 0) pszDest[0] = 0;
	else if (iRet >= cchMax) {
		pszDest[cchMax] = 0;
		iRet = cchMax;
	}
	return iRet;
}

size_t mir_snwprintf(wchar_t *pszDest, const size_t cchDest, const wchar_t *pszFormat, ...)
{
	size_t iRet;
	va_list argList;

	va_start(argList, pszFormat);
	iRet = mir_vsnwprintf(pszDest, cchDest, pszFormat, argList);
		va_end(argList);
	return iRet;
}

int mir_IsEmptyA(char *str)
{
	if(str == NULL || str[0] == 0)
		return 1;
	int i = 0;
	while (str[i]) {
		if (str[i]!=' ' &&
			str[i]!='\r' &&
			str[i]!='\n')
			return 0;
		i++;
	}
	return 1;
}

int mir_IsEmptyW(wchar_t *str)
{
	if(str == NULL || str[0] == 0)
		return 1;
	int i = 0;
	while (str[i]) {
		if (str[i]!=' ' &&
			str[i]!='\r' &&
			str[i]!='\n')
			return 0;
		i++;
	}
	return 1;
}

