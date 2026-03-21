'use strict';
/**
 * Registers the `themeToggle` Alpine.js component once Alpine has initialized.
 *
 * Listening for `alpine:init` guarantees the component is available before
 * Alpine processes any `x-data="themeToggle"` attributes in the DOM.
 *
 * @listens document#alpine:init
 * @param {Event} _e - The `alpine:init` event (unused).
 */
document.addEventListener('alpine:init', (_e) => {
    /**
     * Alpine.js data factory for the light/dark theme toggle component.
     *
     * Reads the initial theme state from the `data-theme` attribute on
     * `<html>` rather than querying `localStorage` directly, keeping the
     * reactive state in sync with whatever theme was applied before first
     * paint.
     *
     * @returns {{ dark: boolean, toggle: function(MouseEvent=): void }}
     *
     * @property {boolean} dark - `true` when dark mode is currently active.
     *
     * @example
     * <button x-data="themeToggle" @click="toggle($event)">
     *     <span x-show="dark">☀️</span>
     *     <span x-show="!dark">🌙</span>
     * </button>
     */
    Alpine.data('themeToggle', () => ({
        dark: document.documentElement.getAttribute('data-theme') === 'dark',
        /**
         * Toggles between light and dark mode, optionally using the View
         * Transitions API to animate the switch as a ripple expanding from
         * the center of the clicked element.
         *
         * Behavior:
         * - The `--ripple-x` and `--ripple-y` CSS custom properties are set
         *   on `<html>` to the center of the toggle button in viewport
         *   coordinates. A `@keyframes` rule in CSS reads these values to
         *   grow a `clip-path` circle outward from that origin.
         * - `document.startViewTransition` captures a screenshot of the old
         *   state, calls `apply()` to switch the theme, then cross-fades
         *   between the two snapshots while the clip-path animation plays.
         * - When the View Transitions API is unavailable, or when called
         *   without a mouse event (e.g. programmatically), `apply()` is
         *   invoked synchronously and no animation occurs.
         *
         * @param {MouseEvent} [event] - The click event from the toggle
         *     button. When present and `document.startViewTransition` is
         *     supported, the ripple transition is played. When absent the
         *     theme switches instantly.
         */
        toggle(event) {
            this.dark = !this.dark;
            /** @type {'dark'|'light'} The incoming theme name. */
            const theme = this.dark ? 'dark' : 'light';
            /**
             * Commits the theme change to the DOM and `localStorage`.
             *
             * Called either directly (no animation) or as the
             * `startViewTransition` callback (inside the transition).
             */
            const apply = () => {
                document.documentElement.setAttribute('data-theme', theme);
                localStorage.setItem('forge-theme', theme);
            };

            if (event && document.startViewTransition) {
                // Place the ripple origin at the center of the toggle button.
                const rect = event.currentTarget.getBoundingClientRect();
                document.documentElement.style.setProperty('--ripple-x', (rect.left + rect.width  / 2) + 'px');
                document.documentElement.style.setProperty('--ripple-y', (rect.top  + rect.height / 2) + 'px');
                // Hand control to the View Transitions API; `apply` runs
                // inside the transition callback so the browser can diff the
                // before/after snapshots and drive the clip-path animation.
                document.startViewTransition(apply);
            } else {
                // Fallback: apply the theme immediately without animation.
                apply();
            }
        }
    }));
});

/**
 * `IntersectionObserver` that triggers CSS entrance animations for every
 * element carrying the `anim-up` class.
 *
 * Entering viewport: adds `visible` to play the transition.
 * Leaving below viewport (user scrolled back up): removes `visible` so the
 * animation replays on the next scroll down.
 * Leaving above viewport (scrolled past): keeps `visible` — already seen.
 */
const animObserver = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            entry.target.classList.add('visible');
        } else if (entry.boundingClientRect.top > 0) {
            entry.target.classList.remove('visible');
        }
    });
}, { threshold: 0.08 });

// Attach the observer to every element that should animate in on scroll.
document.querySelectorAll('.anim-up').forEach(el => animObserver.observe(el));