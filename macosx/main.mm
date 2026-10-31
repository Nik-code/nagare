// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

@import AppKit;

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>

#import "Theme.h"

int main(int argc, char** argv)
{
    tr_lib_init();

    tr_locale_set_global("");

    [Theme install];

    return NSApplicationMain(argc, (char const**)argv);
}
