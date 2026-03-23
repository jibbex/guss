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

/**
 * Animates a `.counter` element's text content from `0` to its `data-target`
 * value using an ease-out cubic easing curve over 900 ms.
 *
 * The animation is driven by `requestAnimationFrame` and updates the element's
 * `textContent` on every frame. An optional `data-suffix` attribute (e.g. `"ms"`)
 * is appended to the displayed value on every frame and on completion.
 *
 * The element receives the `counted` CSS class before the first frame fires,
 * allowing CSS to play a complementary pop-in entrance animation in sync with
 * the number count-up.
 *
 * @param {HTMLElement} el - The counter element to animate. Must have a
 *     `data-target` attribute containing a valid integer string. May optionally
 *     have a `data-suffix` attribute (e.g. `"ms"`) appended after the number.
 * @returns {void} Returns immediately without scheduling a frame if
 *     `data-target` is absent or not a valid integer.
 *
 * @example
 * // HTML: <span class="counter" data-target="506" data-suffix="ms">506ms</span>
 * animateCounter(document.querySelector('.counter'));
 * // → counts 0ms … 253ms … 506ms over 900 ms
 */
function animateCounter(el) {
    const target = parseInt(el.dataset.target, 10);
    const suffix = el.dataset.suffix || '';
    if (isNaN(target)) {
        return;
    }

    const durationInMS = 900;
    const startTime = performance.now();

    /**
     * Single animation frame callback. Computes the eased progress, updates
     * the element's text, and schedules the next frame until complete.
     *
     * @param {DOMHighResTimeStamp} now - Timestamp supplied by `requestAnimationFrame`.
     */
    function tick(now) {
        const elapsed = now - startTime;
        const progress = Math.min(elapsed / durationInMS, 1);
        // ease-out cubic: fast start, decelerates towards the target value
        const eased = 1 - Math.pow(1 - progress, 3);
        const current = Math.round(eased * target);
        el.textContent = current + suffix;
        if (progress < 1) {
            requestAnimationFrame(tick);
        } else {
            el.textContent = target + suffix;
        }
    }

    el.classList.add('counted');
    requestAnimationFrame(tick);
}

/**
 * `IntersectionObserver` that triggers and resets counter animations for every
 * element carrying the `.counter` class.
 *
 * **Entering viewport** (`isIntersecting === true`): calls `animateCounter()`
 * to count up from `0` to `data-target`.
 *
 * **Leaving below the fold** (`boundingClientRect.top > 0`): resets the element's
 * text back to its raw `data-target` value (plus any suffix) and removes the
 * `counted` class so the animation replays the next time the element enters the
 * viewport. Elements that have already been scrolled *past* (top ≤ 0) are left
 * unchanged — they keep their final value.
 *
 * A threshold of `0.5` means the animation starts only once at least half of the
 * element is visible, preventing a premature trigger at the very edge of the
 * viewport.
 *
 * @type {IntersectionObserver}
 *
 * @see animateCounter
 *
 * @example
 * // HTML: <span class="counter gradient-text" data-target="27">27</span>
 * // Automatically observed — no manual call needed.
 */
const counterObserver = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            animateCounter(entry.target);
        } else if (entry.boundingClientRect.top > 0) {
            // Reset when element scrolls back below fold so it replays on re-entry.
            entry.target.classList.remove('counted');
            const suffix = entry.target.dataset.suffix || '';
            entry.target.textContent = (entry.target.dataset.target || '0') + suffix;
        }
    });
}, { threshold: 0.5 });

document.querySelectorAll('.counter').forEach(el => counterObserver.observe(el));