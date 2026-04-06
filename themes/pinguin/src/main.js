'use strict';
import Alpine from '@alpinejs/csp';


/**
 * Alpine.js data component for managing navigation state.
 *
 * @returns {object} The navigation component definition.
 * @property {boolean} open - Whether the navigation is currently open.
 *
 * @example
 * // In your HTML:
 * // <nav x-data="nav">
 * //   <button @click="toggle">Menu</button>
 * //   <ul x-show="open" @click.outside="close">...</ul>
 * // </nav>
 */
Alpine.data('nav', () => ({

    /**
     * Tracks the open/closed state of the navigation.
     * @type {boolean}
     */
    open: false,

    /**
     * Toggles the navigation between open and closed.
     * @returns {void}
     */
    toggle() {
        this.open = !this.open;
    },

    /**
     * Closes the navigation.
     * @returns {void}
     */
    close() {
        this.open = false;
    },

}));

/**
 * Alpine.js bind directive for a dark/light theme toggle button.
 *
 * Toggles the `dark` class on `<html>` and persists the chosen theme to
 * `localStorage`. When the View Transitions API is available, the switch is
 * animated with a ripple effect originating from the button's centre.
 * To avoid conflicts, any element with an existing `view-transition-name`
 * style has its name suppressed for the duration of the transition and then
 * restored afterwards.
 *
 * @example
 * // In your HTML:
 * // <button x-bind="themeToggle">Toggle theme</button>
 *
 * @example
 * // Required CSS custom properties on :root / html:
 * // --ripple-x  – horizontal origin of the view-transition ripple (px)
 * // --ripple-y  – vertical origin of the view-transition ripple (px)
 *
 * @returns {{
 *   type: string,
 *   'aria-pressed': boolean,
 *   '@click': function(MouseEvent): void
 * }} Alpine bind object for the theme toggle button.
 */
Alpine.bind('themeToggle', () => ({

    /**
     * The HTML element type this binding targets.
     * @type {string}
     */
    type: 'button',

    /**
     * Reflects whether dark mode is currently active.
     * Evaluated once at bind time by inspecting the `dark` class on `<html>`.
     * @type {boolean}
     */
    'aria-pressed': document.documentElement.classList.contains('dark'),

    /**
     * Handles a click event on the theme toggle button.
     *
     * Determines the next theme state and delegates to {@link apply}.
     * If a `MouseEvent` is provided and `document.startViewTransition` is
     * supported, the theme switch is wrapped in an animated view transition;
     * otherwise the change is applied synchronously.
     *
     * @param {MouseEvent} event - The click event fired by the button.
     * @returns {void}
     */
    '@click'(event) {

        const html = document.documentElement;

        /**
         * `true` when the page is currently in light mode and is about to
         * switch to dark; `false` for the reverse direction.
         * @type {boolean}
         */
        const light = !html.classList.contains('dark');

        /**
         * The theme toggle button element, queried at click time to ensure the
         * most up-to-date state is captured (e.g. if the button itself is
         * conditionally rendered or moved in the DOM). The presence of the
         * `x-bind="themeToggle"` attribute is used to reliably identify the
         * correct element, even if multiple buttons are present or the structure
         * changes.
         * @type {HTMLElement | null}
         */
        const button = document.querySelector('button[x-bind="themeToggle"]');

        /**
         * Applies the theme change to the DOM and persists it.
         * Toggles the `dark` class on `<html>` and writes the new preference
         * to `localStorage` under the key `"theme"`.
         *
         * @inner
         * @returns {void}
         */
        const apply = () => {
            try {
                const pressed = button?.getAttribute('aria-pressed') === 'true';
                html.classList.toggle('dark', light);
                button?.setAttribute('aria-pressed', !pressed);
                localStorage.setItem('theme', light ? 'dark' : 'light');
            } catch (e) {
                // Fail silently if localStorage is unavailable or quota is exceeded.
                console.warn('Failed to persist theme preference:', e);
            }
        };

        if (event && document.startViewTransition) {

            /**
             * All elements that currently carry an explicit
             * `view-transition-name` inline style. Their names are
             * temporarily cleared to prevent conflicts with the ripple
             * transition and restored once the transition is complete.
             * @type {Element[]}
             */
            const namedEls = Array.from(
                document.querySelectorAll('[style*="view-transition-name"]')
            );

            /**
             * Snapshot of each element's original `view-transition-name`
             * value, preserved in index-matched order for later restoration.
             * @type {string[]}
             */
            const saved = namedEls.map(
                el => el.style.getPropertyValue('view-transition-name')
            );

            // Suppress existing view-transition names for the duration.
            namedEls.forEach(el => el.style.setProperty('view-transition-name', 'none'));

            // Calculate the ripple origin from the button's bounding rect.
            const rect = event.currentTarget.getBoundingClientRect();
            html.style.setProperty('--ripple-x', (rect.left + rect.width / 2) + 'px');
            html.style.setProperty('--ripple-y', (rect.top + rect.height / 2) + 'px');

            html.classList.add('theme-transitioning');

            /**
             * The active view transition wrapping the theme change.
             * @type {ViewTransition}
             */
            const transition = document.startViewTransition(apply);

            // Clean up once the transition has fully completed.
            transition.finished.finally(() => {
                html.classList.remove('theme-transitioning');
                namedEls.forEach((el, i) => {
                    if (saved[i]) {
                        el.style.setProperty('view-transition-name', saved[i]);
                    } else {
                        el.style.removeProperty('view-transition-name');
                    }
                });
            });

        } else {
            // View Transitions API unavailable — apply the change immediately.
            apply();
        }
    }
}));

/**
 * Alpine.js data component for a "back to top" button.
 *
 * Tracks the window scroll position and exposes a `visible` flag that
 * becomes `true` once the user has scrolled past `scrollThreshold` px. A passive scroll
 * listener is registered on `init` and the flag is evaluated immediately
 * so the button state is correct on page load (e.g. after a browser
 * scroll-position restore).
 *
 * @returns {object} The back-to-top component definition.
 *
 * @example
 * // In your HTML:
 * // <button x-data="backToTop(200)" x-show="visible" @click="scrollToTop">
 * //   ↑ Top
 * // </button>
 */
Alpine.data('backToTop', (scrollThreshold = 300) => ({

    /**
     * Whether the button should be visible.
     * `true` when `window.scrollY` exceeds `scrollThreshold` px, `false` otherwise.
     * @type {boolean}
     */
    visible: false,

    /**
     * Scroll position threshold (in pixels) for showing the button.
     * @type {number}
     */
    scrollThreshold: scrollThreshold,

    /**
     * Smoothly scrolls the window back to the top of the page.
     * @returns {void}
     */
    scrollToTop() {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    },

    /**
     * Lifecycle hook called by Alpine when the component is initialised.
     *
     * Registers a passive `scroll` event listener on `window` that updates
     * {@link visible} only when its value actually needs to change, avoiding
     * unnecessary Alpine reactivity triggers. The update callback is also
     * invoked once immediately to synchronise state with the current scroll
     * position.
     *
     * @returns {void}
     */
    init() {

        /**
         * Evaluates the current scroll position and toggles {@link visible}
         * only when the threshold boundary (`scrollThreshold` px) is crossed.
         *
         * @inner
         * @returns {void}
         */
        const update = () => { 
            if ((window.scrollY > this.scrollThreshold) !== this.visible) {
                this.visible = !this.visible; 
            }
        };

        window.addEventListener('scroll', update, { passive: true });
        update();
    }
}));

/**
 * Initialises custom scrollbar visibility behaviour.
 *
 * Adds the `is-scrolling` class to `<html>` while the user is actively
 * scrolling or hovering near the scrollbar track, and removes it after a
 * short idle delay. A permanent `sb-was-active` class is added on first
 * interaction so CSS can distinguish "never scrolled" from "scrolled and
 * idle" states.
 *
 * Two passive event listeners drive the logic:
 * - `scroll`     — reveals the scrollbar and starts the hide timer.
 * - `mousemove`  — keeps the scrollbar visible while the pointer is within
 *                  6 px of the scrollbar track, and starts the hide timer
 *                  once the pointer moves away.
 *
 * @listens window#scroll
 * @listens window#mousemove
 *
 * @example
 * // Typical CSS usage:
 * // html:not(.is-scrolling) ::-webkit-scrollbar-thumb { opacity: 0; }
 */
(function initScrollbars() {

    /**
     * Milliseconds of scroll/hover inactivity before the scrollbar is hidden.
     * Applied when the pointer leaves the scrollbar track.
     * @constant {number}
     */
    const SCROLLBAR_HIDE_DELAY_MS = 600;

    /** @type {HTMLElement} Cached reference to the root `<html>` element. */
    const html = document.documentElement;

    /**
     * Handle for the active `setTimeout` hide timer.
     * `undefined` when no timer is pending.
     * @type {ReturnType<typeof setTimeout> | undefined}
     */
    let timer = undefined;

    /**
     * Whether the pointer is currently positioned over the scrollbar track.
     * While `true`, the hide timer is suppressed so the scrollbar stays
     * visible for as long as the user hovers near it.
     * @type {boolean}
     */
    let nearBar = false;

    /**
     * Removes the `is-scrolling` class from `<html>` and resets
     * {@link nearBar}, effectively hiding the custom scrollbar.
     * Called by the hide timer after the configured delay.
     *
     * @returns {void}
     */
    function hide() {
        html.classList.remove('is-scrolling');
        nearBar = false;
    }

    /**
     * Handles `window` scroll events.
     *
     * Adds `is-scrolling` and `sb-was-active` to `<html>`, resets any
     * pending hide timer, and schedules a new one — unless the pointer is
     * currently near the scrollbar track, in which case the timer is
     * intentionally withheld.
     *
     * @returns {void}
     */
    function handleScroll() {
        html.classList.add('is-scrolling', 'sb-was-active');
        clearTimeout(timer);
        if (!nearBar) {
            timer = setTimeout(hide, SCROLLBAR_HIDE_DELAY_MS);
        }
    }

    /**
     * Handles `window` mousemove events.
     *
     * Computes the scrollbar track width from the difference between
     * `window.innerWidth` and `document.documentElement.clientWidth`, then
     * checks whether the pointer is within a 6 px proximity threshold of
     * the track's left edge.
     *
     * - When the pointer **enters** proximity: sets {@link nearBar}, reveals
     *   the scrollbar, and cancels the hide timer.
     * - When the pointer **leaves** proximity: clears {@link nearBar} and
     *   starts a {@link SCROLLBAR_HIDE_DELAY_MS} hide timer.
     *
     * @param {MouseEvent} evt - The mousemove event fired by the window.
     * @returns {void}
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

    window.addEventListener('scroll', handleScroll, { passive: true });
    window.addEventListener('mousemove', handleMouseMove, { passive: true });
})();

window.Alpine = Alpine;
Alpine.start();