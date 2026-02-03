## 2024-10-26 - [Dark Mode Focus Indicators]
**Learning:** Default browser focus rings are often invisible on dark backgrounds, making keyboard navigation impossible.
**Action:** Always add high-contrast `:focus-visible` styles using the theme's accent color for dark mode interfaces.

## 2024-10-27 - [Sidebar Navigation Semantics]
**Learning:** This app uses `onclick` on `div` elements for sidebar navigation, which breaks keyboard accessibility.
**Action:** Convert sidebar `div`s to `<button>` elements and apply CSS resets (`background: none; border: none; width: 100%; text-align: left;`) to maintain the design while gaining native accessibility.

## 2024-10-28 - [Physical Keyboard Mapping]
**Learning:** Users expect physical keyboard numpads to work seamlessly with on-screen keypads.
**Action:** Always map physical key events (keydown) to the corresponding virtual keypad buttons to improve accessibility and usability.
