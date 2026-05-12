#pragma once
#include <cstdint>
#include <string>

enum class ErrorCode : uint16_t {
    // General (0x00)
    DB_OK              = 0x0000,
    DB_UNKNOWN_ERROR   = 0x0001,
    DB_INVALID_PARAM   = 0x0002,
    DB_NOT_IMPLEMENTED = 0x0003,

    // File I/O (0x10)
    DB_ERR_FILE_NOT_FOUND      = 0x0010,
    DB_ERR_FILE_ALREADY_EXISTS = 0x0011,
    DB_ERR_FILE_OPEN_FAILED    = 0x0012,
    DB_ERR_FILE_READ_FAILED    = 0x0013,
    DB_ERR_FILE_WRITE_FAILED   = 0x0014,
    DB_ERR_FILE_CORRUPTED      = 0x0015,

    // Storage layer (0x20)
    DB_ERR_PAGE_NOT_FOUND    = 0x0020,
    DB_ERR_BUFFER_FULL       = 0x0021,
    DB_ERR_DICT_LOAD_FAILED  = 0x0022,
    DB_ERR_TABLE_NOT_IN_DICT = 0x0023,

    // DDL (0x30)
    DB_ERR_TABLE_EXISTS      = 0x0030,
    DB_ERR_TABLE_NOT_FOUND   = 0x0031,
    DB_ERR_DB_EXISTS         = 0x0032,
    DB_ERR_DB_NOT_FOUND      = 0x0033,
    DB_ERR_FIELD_COUNT_ZERO  = 0x0034,
    DB_ERR_DUPLICATE_FIELD   = 0x0035,

    // DML (0x40)
    DB_ERR_RECORD_INSERT_FAILED = 0x0040,
    DB_ERR_RECORD_NOT_FOUND     = 0x0041,
    DB_ERR_RECORD_READ_FAILED   = 0x0042,
    DB_ERR_TYPE_MISMATCH        = 0x0043,

    // Constraints (0x60)
    DB_ERR_PK_VIOLATION       = 0x0060,
    DB_ERR_NOT_NULL_VIOLATION = 0x0061,

    // Index (0x70)
    DB_ERR_INDEX_EXISTS       = 0x0070,
    DB_ERR_INDEX_NOT_FOUND    = 0x0071,
    DB_ERR_INDEX_FILE_CORRUPTED = 0x0072,
};

inline const char* getErrorMessage(ErrorCode err) {
    switch (err) {
        case ErrorCode::DB_OK:                    return "OK";
        case ErrorCode::DB_UNKNOWN_ERROR:         return "Unknown error";
        case ErrorCode::DB_INVALID_PARAM:         return "Invalid parameter";
        case ErrorCode::DB_NOT_IMPLEMENTED:       return "Not implemented";

        case ErrorCode::DB_ERR_FILE_NOT_FOUND:          return "File not found";
        case ErrorCode::DB_ERR_FILE_ALREADY_EXISTS:     return "File already exists";
        case ErrorCode::DB_ERR_FILE_OPEN_FAILED:        return "Failed to open file";
        case ErrorCode::DB_ERR_FILE_READ_FAILED:        return "Failed to read file";
        case ErrorCode::DB_ERR_FILE_WRITE_FAILED:       return "Failed to write file";
        case ErrorCode::DB_ERR_FILE_CORRUPTED:          return "File corrupted";

        case ErrorCode::DB_ERR_PAGE_NOT_FOUND:     return "Page not found in buffer pool";
        case ErrorCode::DB_ERR_BUFFER_FULL:        return "Buffer pool full";
        case ErrorCode::DB_ERR_DICT_LOAD_FAILED:   return "Failed to load data dictionary";
        case ErrorCode::DB_ERR_TABLE_NOT_IN_DICT:  return "Table metadata not found";

        case ErrorCode::DB_ERR_TABLE_EXISTS:         return "Table already exists";
        case ErrorCode::DB_ERR_TABLE_NOT_FOUND:      return "Table does not exist";
        case ErrorCode::DB_ERR_DB_EXISTS:            return "Database already exists";
        case ErrorCode::DB_ERR_DB_NOT_FOUND:         return "Database does not exist";
        case ErrorCode::DB_ERR_FIELD_COUNT_ZERO:      return "Field count cannot be zero";
        case ErrorCode::DB_ERR_DUPLICATE_FIELD:       return "Duplicate field name";

        case ErrorCode::DB_ERR_RECORD_INSERT_FAILED:  return "Record insert failed";
        case ErrorCode::DB_ERR_RECORD_NOT_FOUND:      return "Record not found";
        case ErrorCode::DB_ERR_RECORD_READ_FAILED:    return "Record read failed";
        case ErrorCode::DB_ERR_TYPE_MISMATCH:         return "Type mismatch";

        case ErrorCode::DB_ERR_PK_VIOLATION:          return "Primary key conflict";
        case ErrorCode::DB_ERR_NOT_NULL_VIOLATION:    return "NOT NULL violation";

        case ErrorCode::DB_ERR_INDEX_EXISTS:          return "Index already exists";
        case ErrorCode::DB_ERR_INDEX_NOT_FOUND:       return "Index not found";
        case ErrorCode::DB_ERR_INDEX_FILE_CORRUPTED:  return "Index file corrupted";

        default:                              return "Undefined error code";
    }
}
