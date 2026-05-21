<script setup>
import { computed, ref } from "vue";

const props = defineProps({
    currentTable: {
        type: String,
        default: "",
    },
    sqlText: {
        type: String,
        default: "",
    },
    loading: {
        type: Boolean,
        default: false,
    },
    scriptLoading: {
        type: Boolean,
        default: false,
    },
    expanded: {
        type: Boolean,
        default: false,
    },
    panelHeight: {
        type: Number,
        default: 58,
    },
    result: {
        type: Object,
        default: () => ({
            headers: [],
            rows: [],
            message: "",
            error: "",
            results: [],
        }),
    },
    scriptResults: {
        type: Array,
        default: () => [],
    },
});

const emit = defineEmits([
    "update:sqlText",
    "run",
    "run-script",
    "fill-current",
    "toggle",
    "resize-start",
]);

const fileInput = ref(null);
const activeScriptTab = ref("tab-1");

function triggerFileUpload() {
    fileInput.value?.click();
}

function handleFileChange(event) {
    const file = event.target.files?.[0];
    if (file) {
        emit("run-script", file);
    }
    // 重置以允许重复选择同一文件
    event.target.value = "";
}

const resultColumns = computed(() => props.result.headers.map((header) => ({
    title: header,
    dataIndex: header,
    key: header,
    ellipsis: true,
})));

const resultSource = computed(() => props.result.rows.map((row, rowIndex) => {
    const record = { key: `sql-row-${rowIndex}` };
    props.result.headers.forEach((header, headerIndex) => {
        record[header] = row[headerIndex] ?? "";
    });
    return record;
}));

// 多结果集支持：返回所有查询结果（优先使用 results 数组，向后兼容单结果）
const displayResults = computed(() => {
    const results = props.result.results;
    if (Array.isArray(results) && results.length > 0) {
        return results;
    }
    // 单结果 fallback
    if (props.result.headers.length > 0) {
        return [{ headers: props.result.headers, rows: props.result.rows }];
    }
    return [];
});

// 给定一个结果对象，生成表格列定义
function makeColumns(res) {
    return (res.headers || []).map((header) => ({
        title: header,
        dataIndex: header,
        key: header,
        ellipsis: true,
    }));
}

// 给定一个结果对象，生成表格数据源
function makeDataSource(res, idx) {
    return (res.rows || []).map((row, rowIndex) => {
        const record = { key: `sql-result-${idx}-row-${rowIndex}` };
        (res.headers || []).forEach((header, headerIndex) => {
            record[header] = row[headerIndex] ?? "";
        });
        return record;
    });
}

const collapsedSummary = computed(() => {
    if (props.result.error) {
        return "上次执行失败";
    }

    if (props.result.message) {
        return props.result.message;
    }

    const results = props.result.results;
    if (Array.isArray(results) && results.length > 1) {
        const totalRows = results.reduce((sum, r) => sum + ((r.rows || []).length), 0);
        return `${results.length} 条查询，共 ${totalRows} 行`;
    }

    if (props.currentTable) {
        return `当前表：${props.currentTable}`;
    }

    return "可执行查询与命令";
});

// 标签页数据：每条 SQL 一个标签
const scriptTabs = computed(() =>
    props.scriptResults.map((r) => {
        const key = `tab-${r.index}`;
        const statusIcon = r.ok ? "✓" : "✗";
        const statusClass = r.ok ? "tab-icon-ok" : "tab-icon-fail";
        const sqlShort = (r.sql || "").length > 36
            ? (r.sql || "").slice(0, 36) + "..."
            : (r.sql || "");
        const title = `[${r.index}] ${statusIcon} ${sqlShort}`;
        return { key, index: r.index, title, statusIcon, statusClass, sql: r.sql || "", ok: r.ok };
    })
);

// 当前选中标签的头部（用于表格渲染）
const activeTabHeaders = computed(() => {
    const tab = props.scriptResults.find((r) => `tab-${r.index}` === activeScriptTab.value);
    return tab?.headers || [];
});

const activeTabRows = computed(() => {
    const tab = props.scriptResults.find((r) => `tab-${r.index}` === activeScriptTab.value);
    return tab?.rows || [];
});

const activeTabColumns = computed(() =>
    activeTabHeaders.value.map((header) => ({
        title: header,
        dataIndex: header,
        key: header,
        ellipsis: true,
    }))
);

const activeTabDataSource = computed(() =>
    activeTabRows.value.map((row, rowIndex) => {
        const record = { key: `script-row-${rowIndex}` };
        activeTabHeaders.value.forEach((header, headerIndex) => {
            record[header] = row[headerIndex] ?? "";
        });
        return record;
    })
);

// 当前标签的 msg 或 error
const activeTabMessage = computed(() => {
    const tab = props.scriptResults.find((r) => `tab-${r.index}` === activeScriptTab.value);
    if (!tab) return "";
    return tab.ok ? (tab.msg || "OK") : (tab.error || "FAIL");
});
</script>

<template>
    <section
        class="work-pane sql-pane"
        :class="{ 'is-expanded': expanded, 'is-collapsed': !expanded }"
        :style="{ height: `${panelHeight}px` }"
    >
        <!-- 隐藏的文件选择器，用于上传 SQL 脚本 -->
        <input
            ref="fileInput"
            type="file"
            accept=".sql,.txt"
            style="display:none"
            @change="handleFileChange"
        />
        <button
            type="button"
            class="sql-pane-resizer"
            @mousedown="$emit('resize-start', $event)"
            @touchstart="$emit('resize-start', $event)"
        >
            <span class="sql-pane-resizer__grabber"></span>
        </button>

        <div v-if="expanded" class="pane-head sql-pane-head">
            <div class="sql-pane-heading">
                <p class="pane-label">命令</p>
                <h2>查询与命令</h2>
                <span class="subtle-copy">
                    输入 SQL 语句后执行查询、更新或结构命令。
                </span>
            </div>
            <div class="pane-head-actions">
                <a-button
                    v-if="expanded"
                    size="small"
                    :loading="scriptLoading"
                    @click="triggerFileUpload"
                >
                    上传脚本
                </a-button>
                <a-button
                    v-if="expanded"
                    size="small"
                    @click="$emit('fill-current')"
                    :disabled="!currentTable"
                >
                    填入当前表
                </a-button>
                <a-button
                    size="small"
                    :type="expanded ? 'default' : 'primary'"
                    @click="$emit('toggle')"
                >
                    {{ expanded ? "收起" : "展开" }}
                </a-button>
                <a-button
                    v-if="expanded"
                    size="small"
                    type="primary"
                    :loading="loading"
                    @click="$emit('run')"
                >
                    执行
                </a-button>
            </div>
        </div>

        <div v-else class="sql-pane-collapsed-bar">
            <div class="sql-pane-collapsed-copy">
                <strong class="sql-pane-collapsed-title">查询与命令</strong>
                <span class="sql-pane-summary">{{ collapsedSummary }}</span>
            </div>
            <a-button
                size="small"
                type="primary"
                @click="$emit('toggle')"
            >
                展开
            </a-button>
        </div>

        <div v-if="expanded" class="sql-pane-body">
            <div class="sql-editor">
                <a-textarea
                    :value="sqlText"
                    :rows="5"
                    placeholder="SELECT * FROM students;"
                    @update:value="$emit('update:sqlText', $event)"
                />
            </div>

            <a-alert
                v-if="result.error"
                type="error"
                show-icon
                :message="result.error"
                class="pane-alert"
            />

            <a-alert
                v-else-if="result.message && !scriptResults.length"
                type="success"
                show-icon
                :message="result.message"
                class="pane-alert"
            />

            <!-- 脚本标签页展示 -->
            <div v-if="scriptResults.length" class="script-tabs-wrapper">
                <a-tabs
                    v-model:activeKey="activeScriptTab"
                    size="small"
                    type="card"
                    class="script-tabs"
                >
                    <a-tab-pane
                        v-for="tab in scriptTabs"
                        :key="tab.key"
                    >
                        <template #tab>
                            <span :class="tab.statusClass">
                                {{ tab.title }}
                            </span>
                        </template>

                        <div class="tab-content">
                            <div class="tab-sql-text">
                                <span class="tab-sql-label">SQL：</span>
                                <code>{{ tab.sql }}</code>
                            </div>

                            <a-alert
                                v-if="!tab.ok"
                                type="error"
                                show-icon
                                :message="activeTabMessage"
                                class="pane-alert"
                            />

                            <a-alert
                                v-else-if="!activeTabHeaders.length"
                                type="success"
                                show-icon
                                :message="activeTabMessage"
                                class="pane-alert"
                            />

                            <a-table
                                v-if="tab.ok && activeTabHeaders.length"
                                size="small"
                                :columns="activeTabColumns"
                                :data-source="activeTabDataSource"
                                :pagination="false"
                                :scroll="{ y: 200, x: 'max-content' }"
                                class="tight-table"
                            />
                        </div>
                    </a-tab-pane>
                </a-tabs>
            </div>

            <a-table
                v-else-if="result.headers.length"
                size="small"
                :columns="resultColumns"
                :data-source="resultSource"
                :pagination="false"
                :scroll="{ y: 260, x: 'max-content' }"
                class="tight-table"
            />

            <a-empty
                v-if="displayResults.length === 0 && !result.error"
                :image="false"
                description="执行后在这里显示结果"
            />
        </div>

    </section>
</template>
