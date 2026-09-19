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
      { threshold: 0.14, rootMargin: "0px 0px -6% 0px" }
    );
    document.querySelectorAll(".reveal").forEach((el) => io.observe(el));
  } else {
    document.querySelectorAll(".reveal").forEach((el) => el.classList.add("is-in"));
  }

  const statusEl = document.getElementById("esp-status");
  const joinBtn = document.getElementById("esp-join");
  const leaveBtn = document.getElementById("esp-leave");
  const ctrl = document.getElementById("esp-ctrl");
  const tracksEl = document.getElementById("esp-tracks");
  const nickEl = document.getElementById("esp-nick");
  if (!statusEl || !joinBtn) return;

  const SVC = "7e570001-7e57-4000-8000-00805f9b34fb";
  const CMD = "7e570002-7e57-4000-8000-00805f9b34fb";
  const STATE = "7e570003-7e57-4000-8000-00805f9b34fb";
  const TRACKS = ["蝉", "蟋蟀", "蝼蛄", "螽斯昼", "螽斯夜", "天牛", "蝗虫"];
  const enc = new TextEncoder();
  const dec = new TextDecoder();

  let cmdChar = null;
  let mine = false;

  nickEl.value = sessionStorage.getItem("mx-nick") || "";

  const setStatus = (t) => {
    statusEl.textContent = t;
  };

  const showCtrl = (on) => {
    mine = on;
    ctrl.hidden = !on;
    tracksEl.hidden = !on;
  };

  if (!navigator.bluetooth) {
    joinBtn.disabled = true;
    setStatus("这个浏览器没有网页蓝牙。请用电脑 Edge / 安卓 Chrome。");
    return;
  }

  TRACKS.forEach((name, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = name;
    b.addEventListener("click", () => writeCmd(String(i)));
    tracksEl.appendChild(b);
  });

  function parseState(text) {
    const p = text.trim().split(",");
    if (p.length < 5) return;
    const pos = Number(p[0]);
    const len = Number(p[1]);
    const me = p[2] === "1";
    const left = Number(p[3]);
    const phase = p[4];
    showCtrl(me);
    leaveBtn.disabled = pos <= 0;
    if (me && phase === "w") {
      setStatus("轮到你了。请在 " + left + " 秒内开始，否则自动跳过。");
    } else if (me) {
      setStatus("你的回合，还剩 " + left + " 秒。");
    } else if (pos > 0) {
      setStatus("你排第 " + pos + " 位，共 " + len + " 人。当前还剩约 " + left + " 秒。");
    } else if (cmdChar) {
      setStatus("已连接。点「连接并排队」加入。");
      leaveBtn.disabled = true;
    }
  }

  async function writeCmd(text) {
    if (!cmdChar) {
      setStatus("还没连上匣子。");
      return;
    }
    try {
      await cmdChar.writeValue(enc.encode(text));
    } catch (e) {
      setStatus("发送失败：" + (e && e.message ? e.message : "已断开"));
    }
  }

  joinBtn.addEventListener("click", async () => {
    const nick = (nickEl.value || "访客").trim().slice(0, 6);
    nickEl.value = nick;
    sessionStorage.setItem("mx-nick", nick);
    joinBtn.disabled = true;
    setStatus("请在系统窗口里选择 7MingXia…");
    try {
      const dev = await navigator.bluetooth.requestDevice({
        filters: [{ name: "7MingXia" }],
        optionalServices: [SVC],
      });
      const server = await dev.gatt.connect();
      const svc = await server.getPrimaryService(SVC);
      cmdChar = await svc.getCharacteristic(CMD);
      const state = await svc.getCharacteristic(STATE);
      state.addEventListener("characteristicvaluechanged", (ev) => {
        parseState(dec.decode(ev.target.value));
      });
      await state.startNotifications();
      dev.addEventListener("gattserverdisconnected", () => {
        cmdChar = null;
        showCtrl(false);
        leaveBtn.disabled = true;
        joinBtn.disabled = false;
        setStatus("蓝牙已断开。靠近匣子后可再连。");
      });
      await writeCmd("J" + nick);
      joinBtn.disabled = false;
      joinBtn.textContent = "已连接，重新排队";
    } catch (e) {
      joinBtn.disabled = false;
      if (e && e.name === "NotFoundError") {
        setStatus("没有选中设备。请靠近匣子，确认已刷入带蓝牙的固件。");
      } else {
        setStatus("连接取消或失败。");
      }
    }
  });

  leaveBtn.addEventListener("click", () => writeCmd("L"));
  document.getElementById("esp-play").addEventListener("click", () => writeCmd("P"));
  document.getElementById("esp-stop").addEventListener("click", () => writeCmd("S"));
  document.getElementById("esp-done").addEventListener("click", () => writeCmd("D"));
})();
