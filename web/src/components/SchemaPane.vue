<script setup>
import { computed } from "vue";

const props = defineProps({
    currentTable: {
        type: String,
        default: "",
    },
    schema: {
        type: Array,
        default: () => [],
    },
    loading: {
        type: Boolean,
        default: false,
    },
});

const columns = [
    {
        title: "列名",
        dataIndex: "name",
        key: "name",
        width: 128,
    },
    {
        title: "类型",
        dataIndex: "type",
        key: "type",
        width: 110,
    },
    {
        title: "约束",
        key: "constraints",
        width: 120,
    },
];

const dataSource = computed(() => props.schema);
</script>

<template>
    <section class="work-pane">
        <div class="pane-head">
            <div>
                <p class="pane-label">字段</p>
                <h2>表结构</h2>
            </div>
            <a-tag color="blue">{{ schema.length }} 个字段</a-tag>
        </div>

        <a-spin :spinning="loading" class="pane-fill">
            <a-empty
                v-if="!currentTable"
                :image="false"
                description="选择表后显示结构"
            />

            <a-table
                v-else
                size="small"
                :columns="columns"
                :data-source="dataSource"
                :pagination="false"
                :scroll="{ y: 280 }"
                class="tight-table"
            >
                <template #bodyCell="{ column, record }">
                    <template v-if="column.key === 'constraints'">
                        <div class="constraint-tags">
                            <a-tag v-if="record.primaryKey" color="processing">主键</a-tag>
                            <a-tag v-if="record.notNull" color="red">必填</a-tag>
                            <span v-if="!record.primaryKey && !record.notNull" class="subtle-copy">
                                可空
                            </span>
                        </div>
                    </template>
                </template>
            </a-table>
        </a-spin>
    </section>
</template>
