#pragma once

namespace app
{

struct GalleryOptions
{
    bool panel = false;  // open the sample panel
    bool menu = false;   // open a context menu, over the panel if there is one
    bool states = false; // put a few widgets into hover, focus and selection for a screenshot
};

/// Every widget on one screen, so they can be checked by eye in both themes.
void showWidgetGallery(GalleryOptions const& options);

/// Drives the widgets through real window messages and quits with the failure count.
void runWidgetTests();

} // namespace app
