<script setup>
defineProps({
    databaseDraft: {
        type: String,
        default: "",
    },
    databases: {
        type: Array,
        default: () => [],
    },
    tables: {
        type: Array,
        default: () => [],
    },
    currentDatabase: {
        type: String,
        default: "",
    },
    currentTable: {
        type: String,
        default: "",
    },
    loadingDatabases: {
        type: Boolean,
        default: false,
    },
    loadingTables: {
        type: Boolean,
        default: false,
    },
});

defineEmits([
    "update:databaseDraft",
    "create-database",
    "refresh-databases",
    "select-database",
    "open-table-builder",
    "refresh-tables",
    "select-table",
    "delete-table",
]);
</script>

<template>
    <aside class="explorer-pane">
        <div class="explorer-brand">
            <p class="pane-label">RuankoDB</p>
            <h1>数据库工作台</h1>
        </div>

        <section class="explorer-section">
            <div class="section-head">
                <div>
                    <span class="section-title">数据库</span>
                    <span class="section-subtitle">选择正在使用的数据库</span>
                </div>
                <a-button size="small" type="link" @click="$emit('refresh-databases')">
                    刷新
                </a-button>
            </div>

            <div class="create-inline">
                <a-input
                    size="small"
                    :value="databaseDraft"
                    placeholder="输入数据库名"
                    @update:value="$emit('update:databaseDraft', $event)"
                    @pressEnter="$emit('create-database')"
                />
                <a-button size="small" type="primary" @click="$emit('create-database')">
                    新建库
                </a-button>
            </div>

            <a-spin :spinning="loadingDatabases">
                <div v-if="databases.length" class="entity-list">
                    <button
                        v-for="database in databases"
                        :key="database"
                        class="entity-row"
                        :class="{ 'is-active': currentDatabase === database }"
                        type="button"
                        @click="$emit('select-database', database)"
                    >
                        <span class="entity-name">{{ database }}</span>
                        <span class="entity-meta">{{ currentDatabase === database ? "已连接" : "切换" }}</span>
                    </button>
                </div>

                <a-empty v-else :image="false" description="暂无数据库" />
            </a-spin>
        </section>

        <section class="explorer-section">
            <div class="section-head">
                <div>
                    <span class="section-title">表</span>
                    <span class="section-subtitle">{{ currentDatabase || "未选择数据库" }}</span>
                </div>
                <div class="section-actions">
                    <a-button
                        size="small"
                        @click="$emit('open-table-builder')"
                        :disabled="!currentDatabase"
                    >
                        新建数据表
                    </a-button>
                    <a-button
                        size="small"
                        type="link"
                        @click="$emit('refresh-tables')"
                        :disabled="!currentDatabase"
                    >
                        刷新
                    </a-button>
                </div>
            </div>

            <a-spin :spinning="loadingTables">
                <div v-if="tables.length" class="entity-list">
                    <div
                        v-for="table in tables"
                        :key="table"
                        class="entity-row entity-row-table"
                        :class="{ 'is-active': currentTable === table }"
                    >
                        <button
                            class="entity-row-trigger"
                            type="button"
                            @click="$emit('select-table', table)"
                        >
                            <span class="entity-name">{{ table }}</span>
                            <span class="entity-meta">{{ currentTable === table ? "已选中" : "打开" }}</span>
                        </button>

                        <a-popconfirm
                            title="确认删除该表？"
                            ok-text="删除"
                            cancel-text="取消"
                            @confirm="$emit('delete-table', table)"
                        >
                            <a-button type="text" size="small" danger>
                                删除
                            </a-button>
                        </a-popconfirm>
                    </div>
                </div>

                <a-empty
                    v-else
                    :image="false"
                    :description="currentDatabase ? '当前数据库没有表' : '先选择数据库'"
                />
            </a-spin>
        </section>
    </aside>
</template>
