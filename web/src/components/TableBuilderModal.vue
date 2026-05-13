<script setup>
import { reactive, watch } from "vue";
import { message } from "ant-design-vue";

const props = defineProps({
    open: {
        type: Boolean,
        default: false,
    },
    loading: {
        type: Boolean,
        default: false,
    },
});

const emit = defineEmits(["submit", "cancel"]);

const columnTypes = ["INT", "VARCHAR", "TEXT", "FLOAT", "DOUBLE", "BOOL"];

const form = reactive({
    name: "",
    columns: [],
});

function createColumn(defaults = {}) {
    return {
        key: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
        name: defaults.name ?? "",
        type: defaults.type ?? "VARCHAR",
        primaryKey: defaults.primaryKey ?? false,
        notNull: defaults.notNull ?? false,
    };
}

function resetForm() {
    form.name = "";
    form.columns = [
        createColumn({ name: "id", type: "INT", primaryKey: true, notNull: true }),
        createColumn({ name: "name", type: "VARCHAR", notNull: true }),
    ];
}

function addColumn() {
    form.columns.push(createColumn());
}

function removeColumn(index) {
    if (form.columns.length <= 1) {
        message.error("至少保留一列。");
        return;
    }

    form.columns.splice(index, 1);
}

function togglePrimaryKey(index, checked) {
    form.columns.forEach((column, columnIndex) => {
        column.primaryKey = checked && columnIndex === index;
        if (column.primaryKey) {
            column.notNull = true;
        }
    });
}

function handleSubmit() {
    const name = form.name.trim();
    if (!name) {
        message.error("表名不能为空。");
        return;
    }

    if (form.columns.some((column) => !column.name.trim())) {
        message.error("列名不能为空。");
        return;
    }

    const normalizedColumns = form.columns.map((column) => ({
        name: column.name.trim(),
        type: column.type,
        primaryKey: column.primaryKey,
        notNull: column.notNull,
    }));

    if (new Set(normalizedColumns.map((column) => column.name)).size !== normalizedColumns.length) {
        message.error("列名不能重复。");
        return;
    }

    emit("submit", {
        name,
        columns: normalizedColumns,
    });
}

watch(
    () => props.open,
    (open) => {
        if (open) {
            resetForm();
        }
    },
    { immediate: true },
);
</script>

<template>
    <a-modal
        :open="open"
        title="可视化建表"
        width="860px"
        :confirm-loading="loading"
        ok-text="创建表"
        cancel-text="取消"
        @ok="handleSubmit"
        @cancel="$emit('cancel')"
    >
        <div class="builder-layout">
            <div class="builder-name-row">
                <span class="builder-label">表名</span>
                <a-input
                    v-model:value="form.name"
                    size="small"
                    placeholder="students"
                />
            </div>

            <div class="builder-toolbar">
                <div>
                    <span class="section-title">列定义</span>
                    <span class="section-subtitle">列名、类型、主键、非空</span>
                </div>
                <a-button size="small" @click="addColumn">
                    新增列
                </a-button>
            </div>

            <div class="builder-columns">
                <div
                    v-for="(column, index) in form.columns"
                    :key="column.key"
                    class="builder-column-row"
                >
                    <a-input
                        v-model:value="column.name"
                        size="small"
                        placeholder="列名"
                    />

                    <a-select
                        v-model:value="column.type"
                        size="small"
                        :options="columnTypes.map((type) => ({ label: type, value: type }))"
                    />

                    <label class="builder-check">
                        <span>主键</span>
                        <a-checkbox
                            :checked="column.primaryKey"
                            @update:checked="togglePrimaryKey(index, $event)"
                        />
                    </label>

                    <label class="builder-check">
                        <span>非空</span>
                        <a-checkbox v-model:checked="column.notNull" />
                    </label>

                    <a-button
                        size="small"
                        danger
                        @click="removeColumn(index)"
                    >
                        删除
                    </a-button>
                </div>
            </div>
        </div>
    </a-modal>
</template>
