/*******************************************************************************
 *
 * Created by Daniel Carrasco at https://www.electrosoftcloud.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the Mozilla Public License 2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Mozilla Public License 2.0 for more details.
 *
 * You should have received a copy of the Mozilla Public License 2.0
 * along with this program.  If not, see <https://www.mozilla.org/en-US/MPL/2.0/>.
 *
 ******************************************************************************/

/*******************************************************************************
 * This program is just a test program wich can be hardly improved.
 * As a test, doesn't worth to do it, and the resulting file can be bigger compared
 * with an improved version. For example, the index can be condensed and then avoid
 * to use a lot of bytes to store it. Also some compression can be added and even
 * any kind of CRC check.
 ********************************************************************************/

#include <libloaderapi.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ecm.h"

#include <getopt.h>