<script setup>
import { computed, onBeforeUnmount, reactive, ref, watch } from "vue";
import { message, Modal, theme as antdTheme } from "ant-design-vue";
import { api } from "./api/client";
import { normalizeGrid, normalizeSchema, normalizeStringList } from "./utils/normalizers";
import ExplorerPane from "./components/ExplorerPane.vue";
import SchemaPane from "./components/SchemaPane.vue";
import SqlConsolePane from "./components/SqlConsolePane.vue";
import TableBuilderModal from "./components/TableBuilderModal.vue";

const themeConfig = {
    algorithm: antdTheme.compactAlgorithm,
    token: {
        colorPrimary: "#1668dc",
        borderRadius: 8,
        fontSize: 12,
        colorText: "#0f172a",
        colorTextSecondary: "#475569",
        colorBgBase: "#f3f6fb",
        colorBorderSecondary: "#dbe4f0",
    },
    components: {
        Button: {
            controlHeight: 28,
            paddingInline: 10,
        },
        Input: {
            controlHeight: 30,
        },
        Table: {
            cellPaddingBlock: 8,
            cellPaddingInline: 10,
            headerBg: "#f8fafc",
        },
        Modal: {
            borderRadiusLG: 12,
        },
    },
};

const SQL_PANEL_COLLAPSED_HEIGHT = 58;
const SQL_PANEL_DEFAULT_HEIGHT = 320;
const SQL_PANEL_MAX_HEIGHT = 620;

const databases = ref([]);
const tables = ref([]);
const currentDatabase = ref("");
const currentTable = ref("");
const databaseDraft = ref("");
const schema = ref([]);
const tableGrid = ref({ headers: [], rows: [] });
const editableRows = ref([]);
const insertDraft = ref({});
const sqlText = ref("");
const sqlResult = ref({
    headers: [],
    rows: [],
    message: "",
    error: "",
});
const tableBuilderOpen = ref(false);
const committingCells = ref(new Set());
const sqlPanelHeight = ref(SQL_PANEL_COLLAPSED_HEIGHT);

const loading = reactive({
    databases: false,
    tables: false,
    schema: false,
    data: false,
    sql: false,
    createDatabase: false,
    createTable: false,
});

const activeHeaders = computed(() => {
    if (tableGrid.value.headers.length) {
        return tableGrid.value.headers;
    }

    return schema.value.map((column) => column.name);
});

const tableSummary = computed(() => ({
    rows: tableGrid.value.rows.length,
    columns: activeHeaders.value.length,
}));

const sqlPanelExpanded = computed(() => sqlPanelHeight.value > SQL_PANEL_COLLAPSED_HEIGHT + 16);

const dataTableScrollY = computed(() => {
    const offset = Math.max(0, sqlPanelHeight.value - SQL_PANEL_COLLAPSED_HEIGHT);
    return Math.max(260, 520 - offset);
});

const dataColumns = computed(() => [
    {
        title: "#",
        key: "rowIndex",
        dataIndex: "_rowIndex",
        width: 56,
        fixed: "left",
    },
    ...activeHeaders.value.map((header) => ({
        title: header,
        key: header,
        dataIndex: header,
        ellipsis: false,
    })),
    {
        title: "操作",
        key: "actions",
        width: 78,
        fixed: "right",
    },
]);

watch(
    activeHeaders,
    (headers) => {
        const nextDraft = {};
        headers.forEach((header) => {
            nextDraft[header] = insertDraft.value[header] ?? "";
        });
        insertDraft.value = nextDraft;
    },
    { immediate: true },
);

let detachSqlResizeListeners = null;

void initialize();

onBeforeUnmount(() => {
    stopSqlPanelResize();
});

async function initialize() {
    await loadDatabases({ silent: true });
}

function resetCurrentTable() {
    currentTable.value = "";
    schema.value = [];
    tableGrid.value = { headers: [], rows: [] };
    editableRows.value = [];
}

function syncEditableRows(headers, rows) {
    editableRows.value = rows.map((row, rowIndex) => {
        const record = {
            key: `row-${rowIndex}`,
            _rowIndex: rowIndex,
        };

        headers.forEach((header, headerIndex) => {
            record[header] = row[headerIndex] ?? "";
        });

        return record;
    });
}

function getErrorMessage(error, fallback) {
    if (error instanceof Error && error.message) {
        return error.message;
    }

    return fallback;
}

function getMaxSqlPanelHeight() {
    if (typeof window === "undefined") {
        return SQL_PANEL_MAX_HEIGHT;
    }

    return Math.min(SQL_PANEL_MAX_HEIGHT, Math.max(SQL_PANEL_DEFAULT_HEIGHT, window.innerHeight - 150));
}

function clampSqlPanelHeight(height) {
    return Math.min(Math.max(Math.round(height), SQL_PANEL_COLLAPSED_HEIGHT), getMaxSqlPanelHeight());
}

function setSqlPanelHeight(height) {
    sqlPanelHeight.value = clampSqlPanelHeight(height);
}

function expandSqlPanel(height = SQL_PANEL_DEFAULT_HEIGHT) {
    setSqlPanelHeight(height);
}

function collapseSqlPanel() {
    sqlPanelHeight.value = SQL_PANEL_COLLAPSED_HEIGHT;
}

function toggleSqlPanel() {
    if (sqlPanelExpanded.value) {
        collapseSqlPanel();
        return;
    }

    expandSqlPanel();
}

function getPointerClientY(event) {
    if (event.touches?.length) {
        return event.touches[0].clientY;
    }

    if (event.changedTouches?.length) {
        return event.changedTouches[0].clientY;
    }

    return event.clientY;
}

function stopSqlPanelResize() {
    if (detachSqlResizeListeners) {
        detachSqlResizeListeners();
        detachSqlResizeListeners = null;
    }
}

function startSqlPanelResize(event) {
    const startY = getPointerClientY(event);
    if (typeof startY !== "number") {
        return;
    }

    if (event.cancelable) {
        event.preventDefault();
    }

    const startHeight = sqlPanelHeight.value;

    const handleMove = (moveEvent) => {
        if (moveEvent.cancelable) {
            moveEvent.preventDefault();
        }

        const nextY = getPointerClientY(moveEvent);
        if (typeof nextY !== "number") {
            return;
        }

        const delta = startY - nextY;
        setSqlPanelHeight(startHeight + delta);
    };

    const handleEnd = () => {
        if (sqlPanelHeight.value < SQL_PANEL_COLLAPSED_HEIGHT + 60) {
            collapseSqlPanel();
        }

        stopSqlPanelResize();
    };

    window.addEventListener("mousemove", handleMove);
    window.addEventListener("mouseup", handleEnd);
    window.addEventListener("touchmove", handleMove, { passive: false });
    window.addEventListener("touchend", handleEnd);

    detachSqlResizeListeners = () => {
        window.removeEventListener("mousemove", handleMove);
        window.removeEventListener("mouseup", handleEnd);
        window.removeEventListener("touchmove", handleMove);
        window.removeEventListener("touchend", handleEnd);
    };
}

function handleError(error, fallback) {
    console.error(error);
    message.error(getErrorMessage(error, fallback));
}

function getEditableRow(rowIndex) {
    return editableRows.value[rowIndex] ?? null;
}

function updateEditableCell(rowIndex, key, value) {
    const row = getEditableRow(rowIndex);
    if (row) {
        row[key] = value;
    }
}

function updateInsertField(header, value) {
    insertDraft.value = {
        ...insertDraft.value,
        [header]: value,
    };
}

function buildRowValues(rowIndex) {
    const row = getEditableRow(rowIndex);
    return activeHeaders.value.map((header) => String(row?.[header] ?? ""));
}

async function loadDatabases({ silent = false } = {}) {
    loading.databases = true;

    try {
        const payload = await api.listDatabases();
        databases.value = normalizeStringList(payload, ["databases", "dbs"]);

        if (!databases.value.includes(currentDatabase.value)) {
            currentDatabase.value = "";
            tables.value = [];
            resetCurrentTable();
        }

        if (!silent) {
            message.success(`已加载 ${databases.value.length} 个数据库。`);
        }
    } catch (error) {
        handleError(error, "数据库列表加载失败。");
    } finally {
        loading.databases = false;
    }
}

async function loadTables({ silent = false } = {}) {
    if (!currentDatabase.value) {
        return;
    }

    loading.tables = true;

    try {
        const payload = await api.listTables();
        tables.value = normalizeStringList(payload, ["tables"]);

        if (!tables.value.includes(currentTable.value)) {
            resetCurrentTable();
        }

        if (!silent) {
            message.success(`已加载 ${tables.value.length} 张表。`);
        }
    } catch (error) {
        handleError(error, "表列表加载失败。");
    } finally {
        loading.tables = false;
    }
}

async function switchDatabase(database) {
    if (!database || database === currentDatabase.value) {
        return;
    }

    const previousDatabase = currentDatabase.value;
    const previousTables = [...tables.value];
    const previousCurrentTable = currentTable.value;
    const previousSchema = [...schema.value];
    const previousGrid = {
        headers: [...tableGrid.value.headers],
        rows: tableGrid.value.rows.map((row) => [...row]),
    };
    const previousEditable = editableRows.value.map((row) => ({ ...row }));

    currentDatabase.value = database;
    tables.value = [];
    resetCurrentTable();

    try {
        await api.useDatabase(database);
        await loadTables({ silent: true });
        message.success(`已切换到数据库 ${database}。`);
    } catch (error) {
        currentDatabase.value = previousDatabase;
        tables.value = previousTables;
        currentTable.value = previousCurrentTable;
        schema.value = previousSchema;
        tableGrid.value = previousGrid;
        editableRows.value = previousEditable;
        handleError(error, `切换数据库失败：${database}`);
    }
}

async function createDatabase() {
    const name = databaseDraft.value.trim();
    if (!name) {
        message.error("数据库名称不能为空。");
        return;
    }

    loading.createDatabase = true;

    try {
        await api.createDatabase(name);
        databaseDraft.value = "";
        await loadDatabases({ silent: true });
        await switchDatabase(name);
        message.success(`数据库 ${name} 已创建。`);
    } catch (error) {
        handleError(error, `创建数据库失败：${name}`);
    } finally {
        loading.createDatabase = false;
    }
}

async function loadSchema(table, { silent = true } = {}) {
    loading.schema = true;

    try {
        const payload = await api.getSchema(table);
        if (currentTable.value !== table) {
            return;
        }

        schema.value = normalizeSchema(payload);

        if (!silent) {
            message.success(`已读取 ${table} 的结构。`);
        }
    } catch (error) {
        handleError(error, `读取表结构失败：${table}`);
    } finally {
        loading.schema = false;
    }
}

async function loadData(table, { silent = true } = {}) {
    loading.data = true;

    try {
        const payload = await api.getData(table);
        if (currentTable.value !== table) {
            return;
        }

        tableGrid.value = normalizeGrid(payload, schema.value.map((column) => column.name));
        syncEditableRows(tableGrid.value.headers, tableGrid.value.rows);

        if (!silent) {
            message.success(`已读取 ${table} 的数据。`);
        }
    } catch (error) {
        handleError(error, `读取表数据失败：${table}`);
    } finally {
        loading.data = false;
    }
}

async function selectTable(table) {
    if (!table) {
        return;
    }

    currentTable.value = table;
    schema.value = [];
    tableGrid.value = { headers: [], rows: [] };
    editableRows.value = [];

    await Promise.all([
        loadSchema(table, { silent: true }),
        loadData(table, { silent: true }),
    ]);
}

async function refreshCurrentTable() {
    if (!currentTable.value) {
        message.error("先选择表。");
        return;
    }

    await Promise.all([
        loadSchema(currentTable.value, { silent: true }),
        loadData(currentTable.value, { silent: true }),
    ]);
}

function openTableBuilder() {
    if (!currentDatabase.value) {
        message.error("先选择数据库。");
        return;
    }

    tableBuilderOpen.value = true;
}

function closeTableBuilder() {
    tableBuilderOpen.value = false;
}

async function submitTableBuilder(payload) {
    loading.createTable = true;

    try {
        await api.createTable(payload);
        tableBuilderOpen.value = false;
        await loadTables({ silent: true });
        await selectTable(payload.name);
        message.success(`表 ${payload.name} 已创建。`);
    } catch (error) {
        handleError(error, `创建表失败：${payload.name}`);
    } finally {
        loading.createTable = false;
    }
}

async function requestDeleteTable(table) {
    if (!table) {
        return;
    }

    try {
        await api.deleteTable(table);
        if (currentTable.value === table) {
            resetCurrentTable();
        }
        await loadTables({ silent: true });
        message.success(`表 ${table} 已删除。`);
    } catch (error) {
        handleError(error, `删除表失败：${table}`);
    }
}

function confirmDeleteCurrentTable() {
    if (!currentTable.value) {
        message.error("先选择表。");
        return;
    }

    Modal.confirm({
        title: `确认删除表 ${currentTable.value}？`,
        okText: "删除",
        cancelText: "取消",
        okButtonProps: { danger: true },
        onOk: () => requestDeleteTable(currentTable.value),
    });
}

async function insertRow() {
    if (!currentTable.value) {
        message.error("先选择表。");
        return;
    }

    if (!activeHeaders.value.length) {
        message.error("当前表没有可插入字段。");
        return;
    }

    const values = activeHeaders.value.map((header) => insertDraft.value[header] ?? "");

    try {
        await api.insertRow(currentTable.value, activeHeaders.value, values);
        insertDraft.value = Object.fromEntries(activeHeaders.value.map((header) => [header, ""]));
        await loadData(currentTable.value, { silent: true });
        message.success("记录已插入。");
    } catch (error) {
        handleError(error, "插入数据失败。");
    }
}

async function deleteRow(rowIndex) {
    if (!currentTable.value) {
        return;
    }

    try {
        await api.deleteRow(currentTable.value, rowIndex);
        await loadData(currentTable.value, { silent: true });
        message.success(`第 ${rowIndex} 行已删除。`);
    } catch (error) {
        handleError(error, "删除数据失败。");
    }
}

async function commitCell(rowIndex, key, rawValue) {
    if (!currentTable.value) {
        return;
    }

    const headerIndex = activeHeaders.value.findIndex((header) => header === key);
    if (headerIndex < 0) {
        return;
    }

    const previousValue = String(tableGrid.value.rows[rowIndex]?.[headerIndex] ?? "");
    const nextValue = String(rawValue ?? "");
    if (previousValue === nextValue) {
        return;
    }

    const commitKey = `${rowIndex}:${key}`;
    if (committingCells.value.has(commitKey)) {
        return;
    }

    committingCells.value.add(commitKey);

    try {
        await api.updateRow(currentTable.value, rowIndex, activeHeaders.value, buildRowValues(rowIndex));
        await loadData(currentTable.value, { silent: true });
    } catch (error) {
        const row = getEditableRow(rowIndex);
        if (row) {
            row[key] = previousValue;
        }
        handleError(error, "更新数据失败。");
    } finally {
        committingCells.value.delete(commitKey);
    }
}

function fillCurrentSql() {
    if (!currentTable.value) {
        message.error("先选择表。");
        return;
    }

    sqlText.value = `SELECT * FROM ${currentTable.value};`;
    expandSqlPanel();
}

function queryTouchesCurrentTable(sql, table) {
    const escaped = table.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    return new RegExp(`\\b(from|update|into|table|join)\\s+${escaped}\\b`, "i").test(sql);
}

async function runSql() {
    const sql = sqlText.value.trim();
    if (!sql) {
        message.error("SQL 不能为空。");
        return;
    }

    expandSqlPanel();
    loading.sql = true;
    sqlResult.value = {
        headers: [],
        rows: [],
        message: "",
        error: "",
    };

    try {
        const payload = await api.query(sql);
        const result = normalizeGrid(payload);
        sqlResult.value = {
            headers: result.headers,
            rows: result.rows,
            message: result.message || "SQL 已执行。",
            error: "",
        };

        if (currentTable.value && queryTouchesCurrentTable(sql, currentTable.value)) {
            await Promise.all([
                loadSchema(currentTable.value, { silent: true }),
                loadData(currentTable.value, { silent: true }),
            ]);
        }
    } catch (error) {
        sqlResult.value = {
            headers: [],
            rows: [],
            message: "",
            error: getErrorMessage(error, "SQL 执行失败。"),
        };
    } finally {
        loading.sql = false;
    }
}
</script>

<template>
    <a-config-provider :theme="themeConfig" component-size="small">
        <a-layout class="db-layout">
            <a-layout-sider :width="286" class="db-sider">
                <ExplorerPane
                    v-model:databaseDraft="databaseDraft"
                    :databases="databases"
                    :tables="tables"
                    :current-database="currentDatabase"
                    :current-table="currentTable"
                    :loading-databases="loading.databases || loading.createDatabase"
                    :loading-tables="loading.tables"
                    @create-database="createDatabase"
                    @refresh-databases="loadDatabases"
                    @select-database="switchDatabase"
                    @open-table-builder="openTableBuilder"
                    @refresh-tables="loadTables"
                    @select-table="selectTable"
                    @delete-table="requestDeleteTable"
                />
            </a-layout-sider>

            <a-layout>
                <a-layout-header class="db-header">
                    <div class="header-title">
                        <p class="pane-label">当前内容</p>
                        <h2>{{ currentTable || currentDatabase || "未选择数据库" }}</h2>
                    </div>

                    <div class="header-status">
                        <a-tag v-if="currentDatabase" color="blue">{{ currentDatabase }}</a-tag>
                        <a-tag v-if="currentTable" color="processing">{{ currentTable }}</a-tag>
                        <a-tag>{{ tableSummary.columns }} 个字段</a-tag>
                        <a-tag>{{ tableSummary.rows }} 条记录</a-tag>
                    </div>

                    <div class="header-actions">
                        <a-button
                            @click="refreshCurrentTable"
                            :disabled="!currentTable"
                        >
                            刷新数据表
                        </a-button>
                        <a-button
                            danger
                            @click="confirmDeleteCurrentTable"
                            :disabled="!currentTable"
                        >
                            删除数据表
                        </a-button>
                    </div>
                </a-layout-header>

                <a-layout-content class="db-content">
                    <div class="content-grid">
                        <div class="top-grid">
                            <SchemaPane
                                :current-table="currentTable"
                                :schema="schema"
                                :loading="loading.schema"
                            />

                            <section class="work-pane data-pane">
                                <div class="pane-head">
                                    <div>
                                        <p class="pane-label">记录</p>
                                        <h2>数据表</h2>
                                    </div>
                                    <div class="pane-head-actions">
                                        <a-tag color="blue">{{ currentTable || "未选择数据表" }}</a-tag>
                                        <a-tag>{{ tableSummary.rows }} 条记录</a-tag>
                                    </div>
                                </div>

                                <a-empty
                                    v-if="!currentTable"
                                    :image="false"
                                    description="选择数据表后显示内容"
                                />

                                <template v-else>
                                    <div class="insert-row">
                                        <div class="insert-fields">
                                            <div
                                                v-for="header in activeHeaders"
                                                :key="header"
                                                class="insert-field"
                                            >
                                                <span class="field-caption">{{ header }}</span>
                                                <a-input
                                                    :value="insertDraft[header]"
                                                    @update:value="updateInsertField(header, $event)"
                                                />
                                            </div>
                                        </div>
                                        <a-button type="primary" @click="insertRow">
                                            新增记录
                                        </a-button>
                                    </div>

                                    <a-table
                                        size="small"
                                        :columns="dataColumns"
                                        :data-source="editableRows"
                                        :loading="loading.data"
                                        :pagination="false"
                                        :scroll="{ x: 'max-content', y: dataTableScrollY }"
                                        class="tight-table data-grid"
                                    >
                                        <template #bodyCell="{ column, record }">
                                            <template v-if="column.key === 'rowIndex'">
                                                <span class="row-index">{{ record._rowIndex }}</span>
                                            </template>

                                            <template v-else-if="column.key === 'actions'">
                                                <a-popconfirm
                                                    title="确认删除该行？"
                                                    ok-text="删除"
                                                    cancel-text="取消"
                                                    @confirm="deleteRow(record._rowIndex)"
                                                >
                                                    <a-button type="text" size="small" danger>
                                                        删除
                                                    </a-button>
                                                </a-popconfirm>
                                            </template>

                                            <template v-else>
                                                <a-input
                                                    :value="record[column.dataIndex]"
                                                    @update:value="updateEditableCell(record._rowIndex, column.dataIndex, $event)"
                                                    @blur="commitCell(record._rowIndex, column.dataIndex, $event.target.value)"
                                                    @pressEnter="commitCell(record._rowIndex, column.dataIndex, $event.target.value)"
                                                />
                                            </template>
                                        </template>
                                    </a-table>
                                </template>
                            </section>
                        </div>

                        <div class="sql-dock-host">
                            <SqlConsolePane
                                v-model:sqlText="sqlText"
                                :current-table="currentTable"
                                :loading="loading.sql"
                                :result="sqlResult"
                                :expanded="sqlPanelExpanded"
                                :panel-height="sqlPanelHeight"
                                @run="runSql"
                                @fill-current="fillCurrentSql"
                                @toggle="toggleSqlPanel"
                                @resize-start="startSqlPanelResize"
                            />
                        </div>
                    </div>
                </a-layout-content>
            </a-layout>
        </a-layout>

        <TableBuilderModal
            :open="tableBuilderOpen"
            :loading="loading.createTable"
            @submit="submitTableBuilder"
            @cancel="closeTableBuilder"
        />
    </a-config-provider>
</template>
