#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTDestinations.h"
#include "PFTNavigatorWidget.h"
#include "PFTRoomMarker.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/LineBatchComponent.h"
#include "Components/TextBlock.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

namespace
{
    UPFTSearchButton* FindDuncanClass(FAutomationTestBase& Test, UWidgetTree* Tree)
    {
        TArray<UPFTSearchButton*> Results;
        Tree->ForEachWidget([&Results](UWidget* Widget)
        {
            if (auto* Button = Cast<UPFTSearchButton>(Widget)) Results.Add(Button);
        });
        if (!Test.TestEqual(TEXT("The instructor, day, and start time identify one class"), Results.Num(), 1)) return nullptr;
        Test.TestEqual(TEXT("The result is CSC 1350"), Results[0]->CourseId, FString(TEXT("CSC_1350")));
        Test.TestEqual(TEXT("The class meets in PFT 1200"), Results[0]->RoomId, FString(TEXT("PFT_1200")));
        return Results[0];
    }

    void CheckSettings(FAutomationTestBase& Test, UWidgetTree* Tree)
    {
        auto* SettingsButton = Cast<UButton>(Tree->FindWidget(TEXT("RouteSettingsButton")));
        if (!Test.TestNotNull(TEXT("Settings are available"), SettingsButton)) return;
        SettingsButton->OnClicked.Broadcast();
        Test.TestTrue(TEXT("Settings expand"), Tree->FindWidget(TEXT("RouteSettings"))->IsVisible());
        auto* LargerText = CastChecked<UCheckBox>(Tree->FindWidget(TEXT("LargerDirections")));
        auto* Performance = CastChecked<UCheckBox>(Tree->FindWidget(TEXT("PerformanceMode")));
        auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
        auto* RenderScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
        if (!Test.TestNotNull(TEXT("Render resolution can be adjusted"), RenderScale)) return;
        const float OriginalScale = RenderScale->GetFloat();
        LargerText->OnCheckStateChanged.Broadcast(true);
        Test.TestTrue(TEXT("Larger directions increases the text size"), Hint->GetFont().Size == 24.0f);
        Performance->OnCheckStateChanged.Broadcast(true);
        Test.TestEqual(TEXT("Performance mode lowers rendering resolution"), RenderScale->GetFloat(), FMath::Min(75.0f, OriginalScale));
        Performance->OnCheckStateChanged.Broadcast(false);
        Test.TestEqual(TEXT("Performance mode can restore the original resolution"), RenderScale->GetFloat(), OriginalScale);
        RenderScale->Set(OriginalScale, ECVF_SetByConsole);
        LargerText->OnCheckStateChanged.Broadcast(LargerText->IsChecked());
        Performance->OnCheckStateChanged.Broadcast(Performance->IsChecked());
        RenderScale->Set(OriginalScale, ECVF_SetByConsole);
        SettingsButton->OnClicked.Broadcast();
    }

    void BeginGuidanceProfile()
    {
#if CSV_PROFILER
        FCsvProfiler::Get()->BeginCapture();
#endif
    }

    void FinishGuidanceProfile()
    {
#if CSV_PROFILER
        const auto Capture = FCsvProfiler::Get()->EndCapture();
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture]() { return Capture.IsReady(); }));
#endif
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTGuidedWalkTest, "PFTFirstFloor.GuidedWalk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTGuidedWalkTest::RunTest(const FString& Parameters)
{
    const bool bProfileGuidance = FParse::Param(FCommandLine::Get(), TEXT("PFTProfileGuidance"));
#if !CSV_PROFILER
    if (bProfileGuidance)
    {
        AddError(TEXT("Guidance profiling requires a build with the CSV profiler."));
        return false;
    }
#endif
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    if (!TestNotNull(TEXT("First-floor demo world"), World)) return false;
    auto* Player = World->GetFirstPlayerController();
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-person walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, Character, Routes, bProfileGuidance,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        if (Widgets.IsEmpty() && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestEqual(TEXT("Navigator is visible"), Widgets.Num(), 1)) return true;
        auto* Navigator = CastChecked<UPFTNavigatorWidget>(Widgets[0]);
        auto* Tree = Navigator->WidgetTree.Get();
        auto* Search = Cast<UEditableTextBox>(Tree->FindWidget(TEXT("RoomSearch")));
        if (!TestNotNull(TEXT("Room search"), Search)) return true;
        CheckSettings(*this, Tree);
        Search->SetText(FText::FromString(TEXT("Commons")));
        Search->OnTextChanged.Broadcast(Search->GetText());
        UPFTSearchButton* Result = nullptr;
        Tree->ForEachWidget([&](UWidget* Widget)
        {
            auto* Button = Cast<UPFTSearchButton>(Widget);
            if (Button && Button->RoomId == TEXT("PFT_COMMONS")) Result = Button;
        });
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Result]()
        {
            TestTrue(TEXT("Commons search result is clickable"), PFTViewportTest::ClickVisibleWidget(Result));
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Player, Character, Routes]()
        {
            TestFalse(TEXT("The route preview starts closed"), Tree->FindWidget(TEXT("RoutePreview"))->IsVisible());
            if (!TestTrue(TEXT("Preview route is clickable"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RoutePreviewButton"))))) return true;
            TestTrue(TEXT("The route preview opens"), Tree->FindWidget(TEXT("RoutePreview"))->IsVisible());
            TestTrue(TEXT("The preview has a map"), Tree->FindWidget(TEXT("RoutePreviewMap"))->IsVisible());
            const auto* Estimate = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RoutePreviewMessage")));
            const FString Summary = Estimate->GetText().ToString();
            TestTrue(TEXT("The preview estimates walking time and distance"), Summary.Contains(TEXT("walk")) && Summary.EndsWith(TEXT(" m")));
            TestTrue(TEXT("The preview keeps the walking pawn and movement"), Player->GetPawn() == Character && !Player->IsMoveInputIgnored());
            TestFalse(TEXT("Previewing leaves guidance off"), Routes->IsGuiding());
            TestTrue(TEXT("Previewing leaves the active route empty"), Routes->GetRemainingPath().IsEmpty());
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
        {
            auto* Map = Tree->FindWidget(TEXT("RoutePreviewMap"));
            TestTrue(TEXT("The preview map has space to render"), Map->GetCachedGeometry().GetLocalSize().Y >= 184);
            for (const FName Name : {FName(TEXT("RouteButton")), FName(TEXT("RoutePreviewButton")),
                FName(TEXT("ViewRoomButton")), FName(TEXT("ClearRoomButton"))})
            {
                const auto* Button = CastChecked<UButton>(Tree->FindWidget(Name));
                TestTrue(FString::Printf(TEXT("%s fits its label"), *Name.ToString()),
                    Button->GetCachedGeometry().GetLocalSize().X >= Button->GetContent()->GetDesiredSize().X);
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-RoutePreview"), true, true, false);
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
        {
            auto* Button = CastChecked<UButton>(Tree->FindWidget(TEXT("RoutePreviewScaleButton")));
            const auto* Label = CastChecked<UTextBlock>(Button->GetContent());
            TestEqual(TEXT("Preview starts focused on the route"), Label->GetText().ToString(), FString(TEXT("Full floor")));
            TestTrue(TEXT("Full floor is clickable in the preview"), PFTViewportTest::ClickVisibleWidget(Button));
            TestEqual(TEXT("The whole floor offers a return to the route"), Label->GetText().ToString(), FString(TEXT("Focus route")));
            if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-RoutePreviewFullFloor"), true, true, false);
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Routes]()
        {
            auto* Button = CastChecked<UButton>(Tree->FindWidget(TEXT("PreviewStepsButton")));
            const auto* Label = CastChecked<UTextBlock>(Button->GetContent());
            TestTrue(TEXT("Show steps is clickable in the preview"), PFTViewportTest::ClickVisibleWidget(Button));
            TestEqual(TEXT("The preview can switch back to the map"), Label->GetText().ToString(), FString(TEXT("Show map")));
            const auto* Steps = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("PreviewRouteSteps")));
            TestTrue(TEXT("The preview lists a turn and the final leg"),
                Steps->GetText().ToString().Contains(TEXT("then turn")) && Steps->GetText().ToString().EndsWith(TEXT("to the destination")));
            TestFalse(TEXT("Steps hide the map scale control"), Tree->FindWidget(TEXT("RoutePreviewScaleButton"))->IsVisible());
            TestFalse(TEXT("Reading the preview steps leaves guidance off"), Routes->IsGuiding());
            if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-PreviewSteps"), true, true, false);
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Tree, Player, Character, Routes, bProfileGuidance]()
        {
            if (!TestTrue(TEXT("Walk there is clickable"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))))) return true;
            if (!TestTrue(TEXT("The route starts"), Routes->IsGuiding()))
            {
                AddError(Routes->GetMessage().ToString());
                return true;
            }
            TestTrue(TEXT("Guidance keeps the first-person pawn"), Player->GetPawn() == Character);
            TestFalse(TEXT("The player can walk"), Player->IsMoveInputIgnored());
            TestFalse(TEXT("The player can look"), Player->IsLookInputIgnored());
            TestTrue(TEXT("The browser collapses while walking"), Tree->FindWidget(TEXT("RoomBrowser"))->GetVisibility() == ESlateVisibility::Collapsed);
            TestFalse(TEXT("Starting a walk closes the route preview"), Tree->FindWidget(TEXT("RoutePreview"))->IsVisible());
            TestTrue(TEXT("The compact directions remain visible"), Tree->FindWidget(TEXT("DirectionsHud"))->IsVisible());
            TestTrue(TEXT("Stop route remains available"), Tree->FindWidget(TEXT("StopRouteButton"))->IsVisible());

            auto* Trail = FindObject<ULineBatchComponent>(Routes, TEXT("PFTWalkingTrail"));
            if (!TestNotNull(TEXT("The route creates a trail component"), Trail)) return true;
            if (!TestTrue(TEXT("The trail draws multiple arrows"), Trail->BatchedLines.Num() >= 8)) return true;
            const TArray<FBatchedLine> InitialArrows = Trail->BatchedLines;
            auto StationaryTip = MakeShared<FVector>();
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail, InitialArrows]()
            {
                if (!TestTrue(TEXT("Arrows stay visible while standing still"), Trail->BatchedLines.Num() >= 8)) return true;
                const auto& Arrow = Trail->BatchedLines[2];
                TestTrue(TEXT("The arrows move while the player is stationary"),
                    !InitialArrows[2].End.Equals(Arrow.End, 2.0));
                TestTrue(TEXT("The two arms meet at the arrow tip"), Arrow.End.Equals(Trail->BatchedLines[3].Start));
                Routes->SetReducedMotion(true);
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
            {
                const auto* TimeLeft = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteTime")));
                TestTrue(TEXT("The walking panel shows estimated time left"), TimeLeft->GetText().ToString().Contains(TEXT(" sec left"))
                    || TimeLeft->GetText().ToString().Contains(TEXT(" min left")));
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Trail, StationaryTip]()
            {
                if (Trail->BatchedLines.IsValidIndex(2)) *StationaryTip = Trail->BatchedLines[2].End;
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail, StationaryTip]()
            {
                TestTrue(TEXT("Reduced motion keeps the arrows still"),
                    Trail->BatchedLines.IsValidIndex(2) && Trail->BatchedLines[2].End.Equals(*StationaryTip));
                Routes->SetReducedMotion(false);
                return true;
            }));

            auto* Overview = Tree->FindWidget(TEXT("RouteOverview"));
            auto* MapButton = Tree->FindWidget(TEXT("RouteOverviewButton"));
            if (!TestNotNull(TEXT("The route overview exists"), Overview)) return true;
            TestFalse(TEXT("The map starts closed"), Overview->IsVisible());
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, MapButton, Overview]()
            {
                TestTrue(TEXT("Show map is clickable"), PFTViewportTest::ClickVisibleWidget(MapButton));
                TestTrue(TEXT("Show map opens the route overview"), Overview->IsVisible());
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, MapButton, Overview, Player, Character, Routes]()
            {
                TestTrue(TEXT("The map has room to render"), Overview->GetCachedGeometry().GetLocalSize().Y >= 184);
                TestTrue(TEXT("The route map leaves walking active"), Player->GetPawn() == Character
                    && !Player->IsMoveInputIgnored() && !Routes->GetRemainingPath().IsEmpty());
                if (FParse::Param(FCommandLine::Get(), TEXT("PFTVerifyLandmarks")))
                {
                    const auto* Sign = Routes->GetRouteLandmark(Routes->GetRemainingPath());
                    TestTrue(TEXT("The Commons map marks the Zone 1200 sign"), Sign && Sign->Key == TEXT("Zone 1200 sign"));
                }
                if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAOverview")))
                    FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-RouteOverview"), true, true, false);
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Routes, Player]()
            {
                auto* Button = CastChecked<UButton>(Tree->FindWidget(TEXT("RouteOverviewScaleButton")));
                const auto* Label = CastChecked<UTextBlock>(Button->GetContent());
                TestTrue(TEXT("Full floor is clickable during guidance"), PFTViewportTest::ClickVisibleWidget(Button));
                TestEqual(TEXT("The walking map can show the entire floor"), Label->GetText().ToString(), FString(TEXT("Focus route")));
                TestTrue(TEXT("Changing map scale leaves guidance active"), Routes->IsGuiding() && !Player->IsMoveInputIgnored());
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
            {
                auto* Button = CastChecked<UButton>(Tree->FindWidget(TEXT("RouteOverviewScaleButton")));
                const auto* Label = CastChecked<UTextBlock>(Button->GetContent());
                TestTrue(TEXT("Focus route is clickable during guidance"), PFTViewportTest::ClickVisibleWidget(Button));
                TestEqual(TEXT("The walking map can return to the route"), Label->GetText().ToString(), FString(TEXT("Full floor")));
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, MapButton, Overview]()
            {
                TestTrue(TEXT("Hide route is clickable"), PFTViewportTest::ClickVisibleWidget(MapButton));
                TestFalse(TEXT("Hide route restores compact directions"), Overview->IsVisible());
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, MapButton, Overview, bProfileGuidance]()
            {
                TestTrue(TEXT("The overview can be reopened"), PFTViewportTest::ClickVisibleWidget(MapButton));
                TestTrue(TEXT("The overview is visible again"), Overview->IsVisible());
                if (bProfileGuidance) BeginGuidanceProfile();
                return true;
            }));

            if (!bProfileGuidance)
            {
                ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
                ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Routes, Player]()
                {
                    auto* Button = CastChecked<UButton>(Tree->FindWidget(TEXT("WalkingStepsButton")));
                    const auto* Label = CastChecked<UTextBlock>(Button->GetContent());
                    TestTrue(TEXT("Show steps is clickable while walking"), PFTViewportTest::ClickVisibleWidget(Button));
                    TestEqual(TEXT("Live steps can switch back to the map"), Label->GetText().ToString(), FString(TEXT("Show map")));
                    const auto* Steps = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("WalkingRouteSteps")));
                    TestTrue(TEXT("Live steps name an upcoming turn"), Steps->GetText().ToString().Contains(TEXT("then turn")));
                    TestTrue(TEXT("Reading the steps leaves movement active"), Routes->IsGuiding() && !Player->IsMoveInputIgnored());
                    if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                        FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-WalkingSteps"), true, true, false);
                    return true;
                }));
                ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            }

            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Player, Character, Routes, Tree, Trail, bProfileGuidance,
                SawZoneSign = MakeShared<bool>(false), StepCounts = MakeShared<FIntPoint>(0, 0), Deadline = FPlatformTime::Seconds() + 65.0]()
            {
                const FVector Position = Character->GetActorLocation();
                if (!bProfileGuidance && Routes->IsGuiding() && !Routes->IsReplanning())
                {
                    const auto* Steps = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("WalkingRouteSteps")));
                    TArray<FString> Lines;
                    const int32 Count = Steps->GetText().ToString().ParseIntoArrayLines(Lines);
                    if (Count > 0 && Lines.Last() != TEXT("Updating route…"))
                    {
                        if (StepCounts->X == 0) StepCounts->X = Count;
                        StepCounts->Y = Count;
                    }
                }
                if (!*SawZoneSign && Routes->GetMessage().ToString().Contains(TEXT("near Zone 1200 sign")))
                {
                    *SawZoneSign = true;
                    if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                    {
                        FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-LandmarkTurn"), true, true, false);
                    }
                }
                if (Routes->HasArrived())
                {
                    if (FParse::Param(FCommandLine::Get(), TEXT("PFTVerifyLandmarks")))
                    {
                        TestTrue(TEXT("The Commons route names the Zone 1200 sign before a nearby turn"), *SawZoneSign);
                    }
                    TestTrue(TEXT("The player reaches the Commons"), FVector::Dist2D(Position, FVector(14900, -5160, 0)) < 180);
                    TestTrue(TEXT("Arrival clears the arrow trail"), Trail->BatchedLines.IsEmpty());
                    const auto* ArrivalLabel = FindObject<UTextRenderComponent>(Routes, TEXT("PFTArrivalLabel"));
                    TestTrue(TEXT("Arrival marks the destination in the building"), ArrivalLabel && ArrivalLabel->IsVisible());
                    auto* Hint = Cast<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                    if (TestNotNull(TEXT("Walking instructions"), Hint))
                        TestEqual(TEXT("Arrival replaces the walking estimate"), Hint->GetText().ToString(), FString(TEXT("Arrived at PFT Commons.")));
                    AddInfo(FString::Printf(TEXT("Reached the Commons with route guidance at %s"), *Position.ToCompactString()));
                    if (bProfileGuidance)
                    {
                        FinishGuidanceProfile();
                        return true;
                    }
                    TestTrue(TEXT("Passed turns drop out of the walking steps"), StepCounts->Y > 0 && StepCounts->Y < StepCounts->X);
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Tree, Routes, ArrivalLabel]()
                    {
                        TestFalse(TEXT("Arrival closes the walking steps"), Tree->FindWidget(TEXT("RouteOverview"))->IsVisible());
                        TestTrue(TEXT("Arrival clears the overview path"), Routes->GetRemainingPath().IsEmpty());
                        TestTrue(TEXT("Find another room is clickable after arrival"),
                            PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("FindAnotherRoomButton"))));
                        TestTrue(TEXT("Find another room restores the browser"), Tree->FindWidget(TEXT("RoomBrowser"))->IsVisible());
                        TestTrue(TEXT("The previous arrival marker clears"), ArrivalLabel && !ArrivalLabel->IsVisible());
                        Navigator->ChooseRoom(TEXT("PFT_3307"));
                        TestFalse(TEXT("An unsupported room cannot preview a route"),
                            Tree->FindWidget(TEXT("RoutePreviewButton"))->GetParent()->IsVisible());
                        TestTrue(TEXT("An unsupported room explains walking directions are unavailable"),
                            Tree->FindWidget(TEXT("RouteUnavailable"))->IsVisible());
                        TestFalse(TEXT("An unsupported room cannot start a route"), Tree->FindWidget(TEXT("RouteButton"))->IsVisible());
                        return true;
                    }));
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
                    {
                        auto* ClassFilter = CastChecked<UCheckBox>(Tree->FindWidget(TEXT("ClassSearchFilter")));
                        if (!TestTrue(TEXT("Class search opens after arrival"),
                            PFTViewportTest::ClickVisibleWidget(ClassFilter))) return true;
                        auto* Instructor = CastChecked<UEditableTextBox>(Tree->FindWidget(TEXT("InstructorSearch")));
                        Instructor->SetText(FText::FromString(TEXT("Duncan")));
                        Instructor->OnTextChanged.Broadcast(Instructor->GetText());
                        CastChecked<UComboBoxString>(Tree->FindWidget(TEXT("ClassDay")))->SetSelectedOption(TEXT("Wednesday"));
                        CastChecked<UComboBoxString>(Tree->FindWidget(TEXT("ClassStart")))->SetSelectedOption(TEXT("10:30 AM"));
                        return true;
                    }));
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree]()
                    {
                        auto* ClassResult = FindDuncanClass(*this, Tree);
                        if (!ClassResult || !TestTrue(TEXT("The class result is clickable"),
                            PFTViewportTest::ClickVisibleWidget(ClassResult))) return true;
                        const auto* ClassLabel = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("SelectedCourseLabel")));
                        TestTrue(TEXT("The selected class is shown before starting the walk"),
                            ClassLabel->IsVisible() && ClassLabel->GetText().ToString().Contains(TEXT("CSC 1350")));
                        if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                            FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-ClassToWalk"), true, true, false);
                        return true;
                    }));
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Character, Routes]()
                    {
                        if (!TestTrue(TEXT("Walk there is clickable after class search"),
                            PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))))) return true;
                        if (!TestTrue(TEXT("The route to class starts at the Commons"), Routes->IsGuiding())) return true;
                        const FVector ReturnPosition = Character->GetActorLocation();
                        // Leave the route entirely to exercise recovery without depending on manual input.
                        Character->SetActorLocation(ReturnPosition + FVector(0, 0, 5000), false, nullptr, ETeleportType::TeleportPhysics);
                        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
                        auto* Trail = FindObject<ULineBatchComponent>(Routes, TEXT("PFTWalkingTrail"));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail]()
                        {
                            TestTrue(TEXT("Leaving the route shows an update state"), Routes->IsReplanning());
                            TestTrue(TEXT("Arrows stay drawn while the route updates"), Trail && !Trail->BatchedLines.IsEmpty());
                            return true;
                        }));
                        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.7f));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Character, ReturnPosition, Tree, Trail]()
                        {
                            TestTrue(TEXT("A disconnected position offers retry"), Routes->CanRetry());
                            TestTrue(TEXT("Guidance and arrows persist when no new path is available"),
                                Routes->IsGuiding() && Trail && !Trail->BatchedLines.IsEmpty());
                            TestTrue(TEXT("Retry is visible"), Tree->FindWidget(TEXT("RetryRouteButton"))->IsVisible());
                            Character->SetActorLocation(ReturnPosition, false, nullptr, ETeleportType::TeleportPhysics);
                            Character->GetCharacterMovement()->StopMovementImmediately();
                            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                            CastChecked<UButton>(Tree->FindWidget(TEXT("RetryRouteButton")))->OnClicked.Broadcast();
                            TestTrue(TEXT("Retry restarts guidance from the hallway"), Routes->IsGuiding());
                            return true;
                        }));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Character, Tree,
                            Deadline = FPlatformTime::Seconds() + 110.0]()
                        {
                            if (Routes->HasArrived())
                            {
                                const auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                                TestTrue(TEXT("The classroom route arrives at PFT 1200"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT 1200")));
                                return true;
                            }
                            if (!Routes->IsGuiding() || FPlatformTime::Seconds() > Deadline)
                            {
                                AddError(FString::Printf(TEXT("The second route stopped at %s. %s"),
                                    *Character->GetActorLocation().ToCompactString(), *Routes->GetMessage().ToString()));
                                return true;
                            }
                            const FVector Delta = Routes->NextPoint() - Character->GetActorLocation();
                            const FVector Direction = Delta.GetSafeNormal2D();
                            Character->GetController()->SetControlRotation(Direction.Rotation());
                            Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                            return false;
                        }));
                        return true;
                    }));
                    return true;
                }
                if (!Routes->IsGuiding() || Trail->BatchedLines.IsEmpty() || Position.Z < 65 || FPlatformTime::Seconds() > Deadline)
                {
                    if (bProfileGuidance) FinishGuidanceProfile();
                    AddError(FString::Printf(TEXT("Guided walk stopped at %s with next point %s. %s"),
                        *Position.ToCompactString(), *Routes->NextPoint().ToCompactString(), *Routes->GetMessage().ToString()));
                    return true;
                }
                const FVector Delta = Routes->NextPoint() - Position;
                const FVector Direction = Delta.GetSafeNormal2D();
                Player->SetControlRotation(Direction.Rotation());
                Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                return false;
            }));
            return true;
        }));
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTWalkToRoom1218Test, "PFTFirstFloor.WalkToRoom1218",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTWalkToRoom1218Test::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    if (!TestNotNull(TEXT("First-floor demo world"), World)) return false;
    auto* Player = World->GetFirstPlayerController();
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-person walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Character, Routes,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        if (Widgets.IsEmpty() && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestEqual(TEXT("Navigator is visible"), Widgets.Num(), 1)) return true;
        auto* Navigator = CastChecked<UPFTNavigatorWidget>(Widgets[0]);
        Navigator->ChooseRoom(TEXT("PFT_1218"));
        auto* Tree = Navigator->WidgetTree.Get();
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Character, Routes]()
        {
            if (!TestTrue(TEXT("Walk there starts directions to 1218"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))) && Routes->IsGuiding())) return true;
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Character, Routes,
                Deadline = FPlatformTime::Seconds() + 40.0]()
            {
                const FVector Position = Character->GetActorLocation();
                if (Routes->HasArrived())
                {
                    const FVector HallwayStop(10440, -5700, 0);
                    TestTrue(TEXT("The player stops in the hallway by the signed door"),
                        FVector::Dist2D(Position, HallwayStop) <= 65.0 && Position.X < 10500
                        && Character->GetCharacterMovement()->IsMovingOnGround());
                    const auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                    TestTrue(TEXT("The arrival message names PFT 1218"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT 1218")));
                    AddInfo(FString::Printf(TEXT("Reached the Room 1218 hallway stop at %s"), *Position.ToCompactString()));
                    return true;
                }
                if (!Routes->IsGuiding() || Position.Z < 65 || FPlatformTime::Seconds() > Deadline)
                {
                    AddError(FString::Printf(TEXT("Room 1218 route stopped at %s. %s"),
                        *Position.ToCompactString(), *Routes->GetMessage().ToString()));
                    return true;
                }
                const FVector Delta = Routes->NextPoint() - Position;
                const FVector Direction = Delta.GetSafeNormal2D();
                Character->GetController()->SetControlRotation(Direction.Rotation());
                Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                return false;
            }));
            return true;
        }));
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTWalkToRoom1100Test, "PFTFirstFloor.WalkToRoom1100",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTWalkToRoom1100Test::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    auto* Player = World ? World->GetFirstPlayerController() : nullptr;
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-person walking pawn"), Character)) return false;
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;
    auto* Destinations = World->GetSubsystem<UPFTDestinations>();
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    const FString RoomId = TEXT("PFT_1100");
    if (!TestTrue(TEXT("Select the auditorium"), Destinations->SelectRoom(RoomId))) return false;
    const auto* Marker = Destinations->ActiveMarker.Get();
    TestTrue(TEXT("The selected room shows its label without the mesh"), !Marker->IsHidden()
        && Marker->Label->IsVisible() && !Marker->Label->bHiddenInGame && Marker->GetRootComponent()->bHiddenInGame);
    if (!TestTrue(TEXT("Start walking directions to the auditorium"),
        Routes->Start(*Character, RoomId, TEXT("PFT 1100")) && Routes->IsGuiding())) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Character, Routes, Deadline = FPlatformTime::Seconds() + 60.0]()
    {
        const FVector Position = Character->GetActorLocation();
        if (Routes->HasArrived())
        {
            const FVector Doorway(6786, 2034, 0);
            const double Inside = FVector::DotProduct(Position - Doorway, FVector(-0.96, -0.28, 0));
            TestTrue(TEXT("Arrival is just inside the auditorium doorway"),
                Inside > 0 && FVector::Dist2D(Position, Doorway) < 200);
            TestEqual(TEXT("The arrival message names the auditorium"), Routes->GetMessage().ToString(),
                FString(TEXT("Arrived at PFT 1100.")));
            AddInfo(FString::Printf(TEXT("Arrived at %s"), *Position.ToCompactString()));
            return true;
        }
        if (!Routes->IsGuiding() || FPlatformTime::Seconds() > Deadline)
        {
            AddError(FString::Printf(TEXT("The auditorium route stopped at %s. %s"),
                *Position.ToCompactString(), *Routes->GetMessage().ToString()));
            return true;
        }
        const FVector Delta = Routes->NextPoint() - Position;
        const FVector Direction = Delta.GetSafeNormal2D();
        Character->GetController()->SetControlRotation(Direction.Rotation());
        Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
        return false;
    }));
    return true;
}

#endif
