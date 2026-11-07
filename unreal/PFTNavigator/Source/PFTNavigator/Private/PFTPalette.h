#pragma once

#include "CoreMinimal.h"

// Claire's LSU colors from docs/design.md. They live in one place so the
// search panel and the room marker always match. Each one is written as RGB
// with its hex code next to it so it's easy to check against the design doc.
namespace PFTPalette
{
    // LSU Purple #461D7C: controls, outlines, and headings
    inline const FColor Purple(70, 29, 124);

    // LSU Gold #FDD023: selected rooms and the Selected tag only
    inline const FColor Gold(253, 208, 35);

    // Ink #1C1526: text
    inline const FColor Ink(28, 21, 38);
}
