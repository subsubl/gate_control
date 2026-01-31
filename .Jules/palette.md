## 2024-10-26 - [Dark Mode Focus Indicators]
**Learning:** Default browser focus rings are often invisible on dark backgrounds, making keyboard navigation impossible.
**Action:** Always add high-contrast `:focus-visible` styles using the theme's accent color for dark mode interfaces.

## 2024-10-27 - [Keyboard Support for Kiosks]
**Learning:** Kiosk interfaces often block standard keyboard inputs (via readonly inputs), alienating assistive tech and keyboard users.
**Action:** Always add global keydown listeners to map physical keys to virtual keypad actions, including Backspace support.
