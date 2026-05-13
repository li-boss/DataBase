const API_BASE = (import.meta.env.VITE_API_BASE ?? "").replace(/\/+$/, "");

export class ApiError extends Error {
    constructor(message, options = {}) {
        super(message);
        this.name = "ApiError";
        this.status = options.status ?? 0;
        this.payload = options.payload ?? null;
        this.path = options.path ?? "";
    }
}

function isObject(value) {
    return value !== null && typeof value === "object" && !Array.isArray(value);
}

function buildUrl(path) {
    if (/^https?:\/\//i.test(path)) {
        return path;
    }

    return API_BASE ? `${API_BASE}${path}` : path;
}

function readMessage(payload, fallback) {
    if (!isObject(payload)) {
        return fallback;
    }

    const candidate = payload.msg ?? payload.message ?? payload.error;
    return candidate === undefined || candidate === null || candidate === ""
        ? fallback
        : String(candidate);
}

function hasApplicationError(payload) {
    if (!isObject(payload)) {
        return false;
    }

    if ("ok" in payload && payload.ok === false) {
        return true;
    }

    if ("success" in payload && payload.success === false) {
        return true;
    }

    if (!("error" in payload)) {
        return false;
    }

    const error = payload.error;
    return !(
        error === undefined ||
        error === null ||
        error === "" ||
        error === 0 ||
        error === "0" ||
        error === "OK" ||
        error === "ok"
    );
}

async function parsePayload(response, path) {
    const text = await response.text();

    if (!text) {
        return { ok: response.ok };
    }

    try {
        return JSON.parse(text);
    } catch (error) {
        if (response.ok) {
            throw new ApiError("响应不是合法 JSON。", {
                status: response.status,
                payload: text,
                path,
            });
        }

        throw new ApiError(text || "请求失败。", {
            status: response.status,
            payload: text,
            path,
        });
    }
}

async function request(path, options = {}) {
    const headers = new Headers(options.headers ?? {});
    const init = {
        method: options.method ?? "GET",
        headers,
    };
    const url = buildUrl(path);

    if (options.body !== undefined) {
        headers.set("Content-Type", "application/json");
        init.body = JSON.stringify(options.body);
    }

    let response;
    try {
        response = await fetch(url, init);
    } catch (error) {
        throw new ApiError("网络请求失败，请检查后端服务是否启动。", { path: url });
    }

    const payload = await parsePayload(response, url);

    if (!response.ok) {
        throw new ApiError(readMessage(payload, `请求失败，状态码 ${response.status}`), {
            status: response.status,
            payload,
            path: url,
        });
    }

    if (hasApplicationError(payload)) {
        throw new ApiError(readMessage(payload, "接口返回业务错误。"), {
            status: response.status,
            payload,
            path: url,
        });
    }

    return payload;
}

function encodeSegment(segment) {
    return encodeURIComponent(String(segment));
}

function serializeColumns(columns) {
    return columns.map((column) => ({
        name: column.name,
        columnName: column.name,
        type: column.type,
        dataType: column.type,
        primaryKey: Boolean(column.primaryKey),
        pk: Boolean(column.primaryKey),
        notNull: Boolean(column.notNull),
        nullable: !column.notNull,
    }));
}

function serializeRow(headers, values) {
    const record = {};
    headers.forEach((header, index) => {
        record[header] = values[index] ?? "";
    });

    return {
        values: record,
        record,
        row: values,
        columns: headers,
    };
}

export const api = {
    listDatabases() {
        return request("/api/databases");
    },

    createDatabase(name) {
        return request("/api/database", {
            method: "POST",
            body: {
                name,
                db: name,
                database: name,
            },
        });
    },

    useDatabase(name) {
        return request(`/api/use/${encodeSegment(name)}`, {
            method: "POST",
        });
    },

    listTables() {
        return request("/api/tables");
    },

    createTable({ name, columns }) {
        return request("/api/table", {
            method: "POST",
            body: {
                name,
                table: name,
                tableName: name,
                columns: serializeColumns(columns),
            },
        });
    },

    deleteTable(name) {
        return request(`/api/table/${encodeSegment(name)}`, {
            method: "DELETE",
        });
    },

    getSchema(table) {
        return request(`/api/schema/${encodeSegment(table)}`);
    },

    getData(table) {
        return request(`/api/data/${encodeSegment(table)}`);
    },

    insertRow(table, headers, values) {
        return request(`/api/data/${encodeSegment(table)}`, {
            method: "POST",
            body: serializeRow(headers, values),
        });
    },

    updateRow(table, rowIndex, headers, values) {
        return request(`/api/data/${encodeSegment(table)}/${encodeSegment(rowIndex)}`, {
            method: "PUT",
            body: serializeRow(headers, values),
        });
    },

    deleteRow(table, rowIndex) {
        return request(`/api/data/${encodeSegment(table)}/${encodeSegment(rowIndex)}`, {
            method: "DELETE",
        });
    },

    query(sql) {
        return request("/api/query", {
            method: "POST",
            body: {
                sql,
                query: sql,
            },
        });
    },
};
