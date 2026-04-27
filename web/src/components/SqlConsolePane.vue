<script setup>
import { computed } from "vue";

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
        }),
    },
});

defineEmits(["update:sqlText", "run", "fill-current", "toggle", "resize-start"]);

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

const collapsedSummary = computed(() => {
    if (props.result.error) {
        return "上次执行失败";
    }

    if (props.result.message) {
        return props.result.message;
    }

    if (props.currentTable) {
        return `当前表：${props.currentTable}`;
    }

    return "可执行查询与命令";
});
</script>

<template>
    <section
        class="work-pane sql-pane"
        :class="{ 'is-expanded': expanded, 'is-collapsed': !expanded }"
        :style="{ height: `${panelHeight}px` }"
    >
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
                v-else-if="result.message"
                type="success"
                show-icon
                :message="result.message"
                class="pane-alert"
            />

            <a-table
                v-if="result.headers.length"
                size="small"
                :columns="resultColumns"
                :data-source="resultSource"
                :pagination="false"
                :scroll="{ y: 260, x: 'max-content' }"
                class="tight-table"
            />

            <a-empty
                v-else
                :image="false"
                description="执行后在这里显示结果"
            />
        </div>

    </section>
</template>
