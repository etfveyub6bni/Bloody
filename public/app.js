(() => {
    const API_BASE = "/api";
    const SESSION_KEY = "bloody-panel-password";

    const loginView = document.getElementById("loginView");
    const panelView = document.getElementById("panelView");
    const loginForm = document.getElementById("loginForm");
    const passwordInput = document.getElementById("passwordInput");
    const loginMessage = document.getElementById("loginMessage");
    const logoutButton = document.getElementById("logoutButton");
    const activatedCount = document.getElementById("activatedCount");
    const usersList = document.getElementById("usersList");
    const allKeysList = document.getElementById("allKeysList");
    const panelMessage = document.getElementById("panelMessage");
    const generateKeyButton = document.getElementById("generateKeyButton");
    const generatedKey = document.getElementById("generatedKey");
    const copyKeyButton = document.getElementById("copyKeyButton");
    const loginButton = loginForm.querySelector('button[type="submit"]');

    let password = sessionStorage.getItem(SESSION_KEY) || "";

    function showLogin() {
        loginView.classList.remove("hidden");
        panelView.classList.add("hidden");
        passwordInput.focus();
    }

    function showPanel() {
        loginView.classList.add("hidden");
        panelView.classList.remove("hidden");
    }

    function setMessage(element, text, tone = "error") {
        element.textContent = text || "";
        element.dataset.tone = tone;
    }

    async function api(path, options = {}) {
        const response = await fetch(`${API_BASE}${path}`, {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
                "X-Admin-Password": password,
                ...(options.headers || {})
            },
            body: options.body || "{}"
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok || payload.ok === false) {
            const error = new Error(payload.error || payload.message || "server error");
            error.status = response.status;
            throw error;
        }
        return payload;
    }

    function escapeHtml(value) {
        return String(value ?? "")
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll("\"", "&quot;")
            .replaceAll("'", "&#039;");
    }

    function formatDate(value) {
        if (!value) return "нет";
        return new Intl.DateTimeFormat("ru-RU", {
            day: "2-digit",
            month: "2-digit",
            year: "numeric",
            hour: "2-digit",
            minute: "2-digit"
        }).format(new Date(value));
    }

    function emptyRow(text) {
        const row = document.createElement("article");
        row.className = "empty-row";
        row.textContent = text;
        return row;
    }

    function statusText(key) {
        if (key.status === "revoked") return "отвязан";
        if (key.hwid) return "активирован";
        return "не активирован";
    }

    function statusClass(key) {
        if (key.status === "revoked") return "revoked";
        if (key.hwid) return "active";
        return "free";
    }

    function render(keys) {
        const activated = keys.filter((key) => key.hwid && key.status !== "revoked");
        activatedCount.textContent = String(activated.length);
        renderUsers(activated);
        renderKeys(keys);
    }

    function renderUsers(keys) {
        usersList.innerHTML = "";
        if (!keys.length) {
            usersList.appendChild(emptyRow("Пока нет активированных ключей."));
            return;
        }
        keys.forEach((key) => usersList.appendChild(keyRow(key, true)));
    }

    function renderKeys(keys) {
        allKeysList.innerHTML = "";
        if (!keys.length) {
            allKeysList.appendChild(emptyRow("Ключей пока нет."));
            return;
        }
        keys.forEach((key) => allKeysList.appendChild(keyRow(key, false)));
    }

    function keyRow(key, compact) {
        const row = document.createElement("article");
        row.className = "list-row";

        const info = document.createElement("div");
        info.className = "row-main";
        info.innerHTML = `
            <strong>${escapeHtml(key.value)}</strong>
            <span>Статус: ${escapeHtml(statusText(key))}</span>
            <span>HWID: ${escapeHtml(key.hwid || "не привязан")}</span>
            ${compact ? "" : `<span>Создан: ${escapeHtml(formatDate(key.createdAt))}</span>`}
            <span>Активирован: ${escapeHtml(formatDate(key.boundAt))}</span>
            <span>Последний запуск: ${escapeHtml(formatDate(key.lastSeenAt))}</span>
            <span class="pill ${statusClass(key)}">${escapeHtml(statusText(key))}</span>
        `;

        const actions = document.createElement("div");
        actions.className = "row-actions";
        const unbind = document.createElement("button");
        unbind.className = "danger-button";
        unbind.type = "button";
        unbind.textContent = "Отвязать";
        unbind.disabled = !key.hwid;
        unbind.addEventListener("click", async () => {
            if (!window.confirm(`Отвязать ключ ${key.value} от HWID?`)) return;
            unbind.disabled = true;
            try {
                const result = await api("/unbind", { body: JSON.stringify({ key: key.value }) });
                render(result.keys || []);
                setMessage(panelMessage, "Ключ отвязан.", "success");
            } catch (error) {
                setMessage(panelMessage, error.message || "Ошибка отвязки");
                unbind.disabled = false;
            }
        });
        actions.append(unbind);
        row.append(info, actions);
        return row;
    }

    async function loadState() {
        const state = await api("/admin-state");
        render(state.keys || []);
    }

    loginForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        password = passwordInput.value.trim();
        if (!password) {
            setMessage(loginMessage, "Введите пароль");
            return;
        }

        loginButton.disabled = true;
        setMessage(loginMessage, "Проверяем доступ…", "success");
        try {
            await loadState();
            sessionStorage.setItem(SESSION_KEY, password);
            setMessage(loginMessage, "");
            showPanel();
        } catch (error) {
            setMessage(loginMessage, error.status === 401 ? "Неверный пароль" : "Не удалось загрузить данные. Проверьте соединение или попробуйте позже.");
        } finally {
            loginButton.disabled = false;
        }
    });

    logoutButton.addEventListener("click", () => {
        sessionStorage.removeItem(SESSION_KEY);
        password = "";
        passwordInput.value = "";
        generatedKey.value = "";
        copyKeyButton.disabled = true;
        setMessage(panelMessage, "");
        showLogin();
    });

    copyKeyButton.addEventListener("click", async () => {
        try {
            await navigator.clipboard.writeText(generatedKey.value);
            setMessage(panelMessage, "Ключ скопирован.", "success");
        } catch (error) {
            generatedKey.focus();
            generatedKey.select();
            setMessage(panelMessage, "Не удалось скопировать автоматически. Скопируйте выделенный ключ вручную.");
        }
    });

    generateKeyButton.addEventListener("click", async () => {
        generateKeyButton.disabled = true;
        try {
            const result = await api("/generate-key");
            generatedKey.value = result.key;
            copyKeyButton.disabled = false;
            setMessage(panelMessage, "Ключ сгенерирован.", "success");
            try {
                await navigator.clipboard.writeText(result.key);
                setMessage(panelMessage, "Ключ сгенерирован и скопирован.", "success");
            } catch (ignored) {
            }
            render(result.keys || []);
        } catch (error) {
            setMessage(panelMessage, error.message || "Не удалось создать ключ");
        } finally {
            generateKeyButton.disabled = false;
        }
    });

    document.querySelectorAll(".tab").forEach((button) => {
        button.addEventListener("click", () => {
            document.querySelectorAll(".tab").forEach((tab) => tab.classList.toggle("active", tab === button));
            document.querySelectorAll(".tab-page").forEach((page) => {
                page.classList.toggle("active", page.id === `tab-${button.dataset.tab}`);
            });
        });
    });

    if (password) {
        loadState().then(showPanel).catch((error) => {
            if (error.status === 401) {
                sessionStorage.removeItem(SESSION_KEY);
                password = "";
                setMessage(loginMessage, "Сессия истекла. Войдите снова.");
            } else {
                setMessage(loginMessage, "Не удалось загрузить данные. Проверьте соединение или попробуйте позже.");
            }
            showLogin();
        });
    } else {
        showLogin();
    }
})();
