/**
 * Initializes a scroll-reveal effect for all `.reveal` elements on the page.
 *
 * On invocation, every `.reveal` element receives a staggered `transitionDelay`
 * based on its position modulo 3 (0 ms, 80 ms, or 160 ms), so that elements
 * entering the viewport together animate in a cascading sequence rather than
 * all at once.
 *
 * An `IntersectionObserver` watches each element and adds the `is-visible`
 * class as soon as at least 8 % of the element crosses a trigger line that is
 * 40 px above the bottom edge of the viewport. Once an element has been
 * revealed it is unobserved immediately, ensuring the transition plays exactly
 * once regardless of further scrolling.
 *
 * If no `.reveal` elements are present in the document the function returns
 * early without creating an observer.
 */
(function initScrollReveal() {
    /** @type {NodeListOf<HTMLElement>} All elements that should animate in on scroll. */
    const reveals = document.querySelectorAll('.reveal');
    if (!reveals.length) {
        return;
    }
    /**
     * Stagger the CSS transition delay across every reveal element.
     * The delay cycles through 0 ms → 80 ms → 160 ms and then repeats,
     * creating a wave effect when multiple elements enter the viewport together.
     *
     * @param {HTMLElement} el - The element to apply the delay to.
     * @param {number}      i  - The element's index in the NodeList.
     */
    const stagger = (el, i) => el.style.transitionDelay = (i % 3) * 80 + 'ms';
    reveals.forEach(stagger);
    /**
     * Handles `IntersectionObserver` callbacks by revealing each entry that has
     * entered the viewport.
     *
     * When an observed element becomes intersecting, the `is-visible` class is
     * added to trigger its CSS reveal transition. The element is then immediately
     * unobserved so the animation plays exactly once and no further callbacks are
     * fired for it.
     *
     * @param {IntersectionObserverEntry[]} entries - The list of entries provided
     *     by the `IntersectionObserver` on each callback invocation.
     */
    const intersectionCallback = (entries) => entries.forEach((entry) => {
        if (entry.isIntersecting) {
            entry.target.classList.add('is-visible');
            observer.unobserve(entry.target);
        }
    });
    const observer = new IntersectionObserver(intersectionCallback, {
        threshold: 0.08,
        rootMargin: '0px 0px -40px 0px',
    });
    reveals.forEach(el => observer.observe(el));
})();