(() => {
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  if (!reduced && "IntersectionObserver" in window) {
    const io = new IntersectionObserver(
      (entries) => {
        entries.forEach((e) => {
          if (e.isIntersecting) {
            e.target.classList.add("is-in");
            io.unobserve(e.target);
          }
        });
      },
      { threshold: 0.18, rootMargin: "0px 0px -8% 0px" }
    );
    document.querySelectorAll(".reveal").forEach((el) => io.observe(el));
  } else {
    document.querySelectorAll(".reveal").forEach((el) => el.classList.add("is-in"));
  }

  const statusEl = document.getElementById("esp-status");
  const playBtn = document.getElementById("esp-play-all");
  const stopBtn = document.getElementById("esp-stop");

  if (!playBtn) return;

  const setStatus = (msg) => {
    if (statusEl) statusEl.textContent = msg;
  };

  playBtn.addEventListener("click", () => {
    setStatus("请连实体匣热点 7MingXia，在匣内网页点「7鸣匣」标题听彩蛋");
    if (stopBtn) stopBtn.disabled = true;
  });

  if (stopBtn) {
    stopBtn.addEventListener("click", () => {
      setStatus("就绪 · 演示模式（音频只在实体匣播放）");
      stopBtn.disabled = true;
    });
  }
})();
