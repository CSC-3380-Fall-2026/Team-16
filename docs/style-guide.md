# Code Standards

Name: Malek Badawi

Link to style guide(s) inspired by/used:
- https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine
- https://google.github.io/styleguide/jsoncguide.html

## Why these style guides?
Since our project will use Unreal Engine and things such as Blueprints/UMG, it makes sense to follow Epic Games' Unreal Engine Coding Standard as it is most applicable here. It has conventions specifically made for readable and maintainable Unreal Engine projects including but not limited to naming, formatting, comments, file organization... We also referenced Google's JSON Style Guide because we will use JSON to organize and link information about rooms, courses, faculty... We do not intend to follow everything exactly but use the conventions as a guide.

## File Organization Structure
Project files should be organized according to their purpose, with Unreal Engine assets grouped together in clearly named folders for things like Maps, Blueprints, UI, Data... Files relating to the Digital Twin should remain separated from app specific functionality. Any new files should be placed in the most specific appropriate folder. Temp/Test assets should be clearly labeled.

## Naming Conventions
**Variables:** Descriptive names that communicate purpose: DestinationRoom, CurrentFloor, SelectedRoom, SearchResults... Booleans should include b: bIsRoomSelected...

**Functions:** Named clearly to describe action performed using PascalCase: FindRoom(), GenerateRoute(), LoadRoomData()...

**Classes:** Classes should also use PascalCase and be descriptive. Also use Unreal class prefix convention: URoomSearchWidget, ARouteManager, ARoomMarker...

**Files:** Name should match primary class/primary function: RoomSearchWidget, RouteManager... Unreal assets use prefixes that match convention: BP_, WBP_...

Python scripts (like the data check script) just follow normal Python naming, snake_case for functions and files.

## Code Conventions
**Max Nesting Depth:** Max nesting depth should generally be 3 levels. Code should not exceed 3 levels unless for valid reasoning, instead if logic becomes this deeply nested it should be simplified using returns or separated helper functions.

**Comment and Docstring Policy:** Comments should explain why code behaves a certain way in a short description. Should include purpose, parameters, and expected result. Complicated logic should be documented anytime the purpose is not apparently clear. Comments should be updated when associated code changes. Blueprints should use comment boxes.

**Cases when functions should be created:** A function should be created for reasons such as: a block of logic is used more than once, a section of the code performs a distinct task that could easily be named, a block becomes long or complicated where separating would improve readability, the operation is likely to be changed independently later... Functions should perform one primary responsibility and lead to less repeated code.

## JSON
JSON keys use PascalCase (RoomNumber not roomNumber) so they match the Unreal struct names when we import them. Every row has a Name field that works as its ID. See [data-schema.md](data-schema.md) for the fields.
