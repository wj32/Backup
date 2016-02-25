/*
 * Backup -
 *   startup
 *
 * Copyright (C) 2011 wj32
 *
 * This file is part of Backup.
 *
 * Backup is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Backup is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Backup.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "backup.h"
#include <objbase.h>

LONG BkRunCommandLine(
    VOID
    );

int __cdecl wmain(int argc, wchar_t *argv[])
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    PhInitializePhLibEx(PHLIB_INIT_MODULE_FILE_STREAM, 512 * 1024, 16 * 1024);

    return BkRunCommandLine();
}
