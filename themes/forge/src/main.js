'use strict';
import Alpine from 'alpinejs';

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
     * @param {MouseEvent} [event] - The click event from the toggle button.
     */
    toggle(event) {
        this.dark = !this.dark;
        /** @type {'dark'|'light'} The incoming theme name. */
        const theme = this.dark ? 'dark' : 'light';
        /**
         * Commits the theme change to the DOM and `localStorage`.
         */
        const apply = () => {
            try {
                document.documentElement.setAttribute('data-theme', theme);
                localStorage.setItem('forge-theme', theme);
            } catch (e) {
                // If localStorage is unavailable (e.g. in private mode), fail
                // gracefully by applying the theme without persisting it.
                console.warn('Failed to persist theme preference:', e);
            }
        };

        if (event && document.startViewTransition) {
            const rect = event.currentTarget.getBoundingClientRect();
            document.documentElement.style.setProperty('--ripple-x', (rect.left + rect.width  / 2) + 'px');
            document.documentElement.style.setProperty('--ripple-y', (rect.top  + rect.height / 2) + 'px');
            document.startViewTransition(apply);
        } else {
            apply();
        }
    }
}));

window.Alpine = Alpine;
Alpine.start();

/**
 * `IntersectionObserver` that triggers CSS entrance animations for every
 * element carrying the `anim-up` class.
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

document.querySelectorAll('.anim-up').forEach(el => animObserver.observe(el));

/**
 * Animates a `.counter` element's text content from `0` to its `data-target`
 * value using an ease-out cubic easing curve over 900 ms.
 *
 * @param {HTMLElement} el - The counter element to animate.
 */
function animateCounter(el) {
    const target = parseInt(el.dataset.target, 10);
    const suffix = el.dataset.suffix || '';
    if (isNaN(target)) {
        return;
    }

    const durationInMS = 900;
    const startTime = performance.now();

    function tick(now) {
        const elapsed = now - startTime;
        const progress = Math.min(elapsed / durationInMS, 1);
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

const counterObserver = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            animateCounter(entry.target);
        } else if (entry.boundingClientRect.top > 0) {
            entry.target.classList.remove('counted');
            const suffix = entry.target.dataset.suffix || '';
            entry.target.textContent = (entry.target.dataset.target || '0') + suffix;
        }
    });
}, { threshold: 0.5 });

document.querySelectorAll('.counter').forEach(el => counterObserver.observe(el));
