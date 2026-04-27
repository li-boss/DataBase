function toBoolean(value) {
    return value === true || value === 1 || value === "1" || value === "true" || value === "TRUE";
}

export function normalizeStringList(payload, keys) {
    if (Array.isArray(payload)) {
        return payload.map((item) => String(item));
    }

    if (!payload || typeof payload !== "object") {
        return [];
    }

    for (const key of keys) {
        if (!Array.isArray(payload[key])) {
            continue;
        }

        return payload[key]
            .map((item) => {
                if (typeof item === "string" || typeof item === "number") {
                    return String(item);
                }

                if (item && typeof item === "object") {
                    return String(item.name ?? item.table ?? item.database ?? "");
                }

                return "";
            })
            .filter(Boolean);
    }

    return [];
}

export function normalizeSchema(payload) {
    const columns = Array.isArray(payload?.columns) ? payload.columns : Array.isArray(payload) ? payload : [];

    return columns.map((column, index) => {
        if (Array.isArray(column)) {
            return {
                key: `schema-${index}`,
                name: String(column[0] ?? `column_${index + 1}`),
                type: String(column[1] ?? "TEXT").toUpperCase(),
                primaryKey: toBoolean(column[2]),
                notNull: toBoolean(column[3]),
            };
        }

        const current = column && typeof column === "object" ? column : {};
        return {
            key: `schema-${index}`,
            name: String(current.name ?? current.column ?? current.field ?? `column_${index + 1}`),
            type: String(current.type ?? current.dataType ?? current.datatype ?? "TEXT").toUpperCase(),
            primaryKey: toBoolean(current.primaryKey ?? current.pk ?? current.isPrimaryKey ?? current.isPk),
            notNull: toBoolean(current.notNull ?? current.required ?? current.isNotNull),
        };
    });
}

export function normalizeGrid(payload, fallbackHeaders = []) {
    const sourceHeaders = Array.isArray(payload?.hdr)
        ? payload.hdr
        : Array.isArray(payload?.headers)
            ? payload.headers
            : Array.isArray(payload?.columns)
                ? payload.columns.map((item) => (typeof item === "string" ? item : item?.name ?? ""))
                : [];

    const sourceRows = Array.isArray(payload?.row)
        ? payload.row
        : Array.isArray(payload?.rows)
            ? payload.rows
            : Array.isArray(payload?.data)
                ? payload.data
                : [];

    const headers = sourceHeaders.map((item) => String(item)).filter(Boolean);

    if (headers.length === 0 && sourceRows[0] && !Array.isArray(sourceRows[0]) && typeof sourceRows[0] === "object") {
        headers.push(...Object.keys(sourceRows[0]));
    }

    if (headers.length === 0 && fallbackHeaders.length > 0) {
        headers.push(...fallbackHeaders);
    }

    const rows = sourceRows.map((entry) => {
        if (Array.isArray(entry)) {
            return entry.map((cell) => (cell ?? "").toString());
        }

        if (entry && typeof entry === "object") {
            return headers.map((header) => (entry[header] ?? "").toString());
        }

        return [(entry ?? "").toString()];
    });

    if (headers.length === 0 && rows[0]?.length) {
        for (let index = 0; index < rows[0].length; index += 1) {
            headers.push(`column_${index + 1}`);
        }
    }

    return {
        headers,
        rows,
        message: String(payload?.msg ?? payload?.message ?? ""),
    };
}
