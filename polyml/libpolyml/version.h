/*
    Title:  version.h

    Copyright (c) 2000-12
        Cambridge University Technical Services Limited

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef VERSION_H_INCLUDED
#define VERSION_H_INCLUDED

// Poly/ML system interface level
#define POLY_version_number    550
// POLY_version_number is written into all exported files and tested
// when we start up.  The idea is to ensure that if a file is exported
// from one version of the library it will run successfully if linked
// with a different version.
// We currently export version 5.5.0 but will support 5.4, 5.3, 5.2 or 5.1.
#define FIRST_supported_version 510
#define LAST_supported_version  550

#define TextVersion             "5.5.0"

#endif
