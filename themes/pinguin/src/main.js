/**
 * Smoothly scrolls the page back to the top.
 *
 * Intended as the `onclick` handler for a back-to-top button. Delegates to
 * `window.scrollTo` with `behavior: 'smooth'` so the browser animates the
 * transition rather than jumping instantly.
 */
function scrollToTop() {
    window.scrollTo({ top: 0, behavior: 'smooth' });
}
/**
 * Initializes the light/dark theme toggle button.
 *
 * Wires up the `#themeBtn` button so that clicking it toggles the `dark`
 * class on the root `<html>` element, persists the chosen theme to
 * `localStorage`, and keeps the sun/moon icon pair in sync with the
 * current state.  `syncIcons` is also called once on startup so the
 * icons reflect whatever theme was applied before this script ran.
 */
(function initThemeSwitch() {
    /** @type {HTMLButtonElement} - The theme-toggle button element. */
    const btn  = document.getElementById('themeBtn');
    /** @type {HTMLElement} - The sun icon element, shown in dark mode. */
    const sun  = document.getElementById('iconSun');
    /** @type {HTMLElement} - The moon icon element, shown in light mode. */
    const moon = document.getElementById('iconMoon');
    /** @type {HTMLHtmlElement} - The root element whose `dark` class drives the color scheme. */
    const html = document.documentElement;
    /**
     * Synchronizes the sun/moon icon visibility with the current theme.
     * The sun is shown when dark mode is active; the moon when it is not.
     */
    const syncIcons = () => {
        const dark = html.classList.contains('dark');
        sun.classList.toggle('hidden', !dark);
        moon.classList.toggle('hidden', dark);
    };
    /**
     * Toggles the `dark` class on `<html>`, persists the preference, and
     * updates the icon pair.
     *
     * @param {Event} _e - The click event (unused).
     */
    const clickHandler = (_e) => {
        const dark = html.classList.toggle('dark');
        localStorage.setItem('theme', dark ? 'dark' : 'light');
        syncIcons();
    };
    btn.addEventListener('click', clickHandler);
    syncIcons();
})();

/**
 * Initializes custom scrollbar behavior to show a scrolling indicator when the user scrolls
 * or hovers near the scrollbar, and hides it after a period of inactivity.
 */
(function initScrollbars() {
    /** @constant {number} - Milliseconds to wait before hiding the scrollbar after the last scroll or mouse-leave event. */
    const SCROLLBAR_HIDE_DELAY_MS = 600;
    /** @type {HTMLHtmlElement} - Reference to the root `<html>` element, used to toggle theme and scrollbar state classes. */
    const html  = document.documentElement;
    /** @type {ReturnType<typeof setTimeout> | undefined} - Handle for the active hide timer; `undefined` when no timer is pending. */
    let timer   = undefined;
    /** @type {boolean} - Whether the pointer is currently hovering near the native scrollbar. */
    let nearBar = false;
    /**
     * Hides the scrollbar by removing the scrolling indicator class and
     * resetting the near-scrollbar hover state.
     */
    function hide() {
        html.classList.remove('is-scrolling');
        nearBar = false;
    }
    /**
     * Handles the window scroll event.
     *
     * Adds the scrolling indicator classes to the HTML element and schedules
     * the scrollbar to be hidden after 800 ms of inactivity, unless the
     * pointer is currently hovering near the scrollbar.
     *
     * @param {Event} _e - The scroll event (unused).
     */
    function handleScroll(_e) {
        html.classList.add('is-scrolling', 'sb-was-active');
        clearTimeout(timer);
        if (!nearBar) {
            timer = setTimeout(hide, 800);
        }
    }
    /**
     * Handles mouse movement to detect proximity to the scrollbar.
     *
     * When the pointer enters the scrollbar area, the scrolling indicator is shown
     * and any pending hide timer is canceled. When the pointer leaves the scrollbar
     * area, the hide timer is restarted after {@link SCROLLBAR_HIDE_DELAY_MS} ms.
     *
     * @param {MouseEvent} evt - The mousemove event.
     */
    function handleMouseMove(evt) {
        const sbW = window.innerWidth - document.documentElement.clientWidth;
        const over = sbW > 0 && evt.clientX >= window.innerWidth - sbW - 6;
        if (over && !nearBar) {
            nearBar = true;
            html.classList.add('is-scrolling', 'sb-was-active');
            clearTimeout(timer);
        } else if (!over && nearBar) {
            nearBar = false;
            clearTimeout(timer);
            timer = setTimeout(hide, SCROLLBAR_HIDE_DELAY_MS);
        }
    }
    // Event Listeners
    window.addEventListener('scroll', handleScroll, {passive: true});
    window.addEventListener('mousemove', handleMouseMove, {passive: true});
})();