#pragma once

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext, NavPrevious };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  // Samples buttons and (on touch boards) the capacitive digitizer. A completed
  // tap is latched for this frame: it is exposed positionally via wasTapped() and,
  // for taps landing in the on-screen bottom hint-bar strip, synthesized into a
  // logical Back/Confirm/Left/Right button edge so menu/list/home UIs are fully
  // navigable by touch (the M5Paper has no physical Back/Left/Right). Inert on
  // button-only boards.
  void update() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // True if a capacitive tap completed during the most recent update(), writing
  // its position in LOGICAL screen coordinates (current orientation). Lets an
  // activity handle location-aware taps (e.g. the reader's page-turn zones)
  // directly. Always false on button-only boards.
  bool wasTapped(int& logicalX, int& logicalY) const;

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;

  // Whether the synthesized touch button captured in the last update() satisfies
  // a query for `button` (directly, or via the NavNext/NavPrevious composition).
  bool touchSynthMatches(Button button) const;

  // Per-frame touch state, refreshed by update(). Mutable because the input query
  // methods are const (they mirror the button path, which reads hardware through a
  // const HalGPIO&) but must observe the tap captured this frame.
  mutable bool tapPending = false;         // a tap completed this frame
  mutable int tapX = 0;                    // logical coords of the tap
  mutable int tapY = 0;                    //
  mutable bool synthButtonValid = false;   // tap mapped to a synthesized button
  mutable Button synthButton = Button::Back;
};
