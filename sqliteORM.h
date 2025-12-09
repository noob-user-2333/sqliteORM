//
// Created by user on 2025/12/3.
//

#ifndef NETSAMPLE_SQLITEORM_H
#define NETSAMPLE_SQLITEORM_H
#include <boost/pfr.hpp>
#include <boost/type_index.hpp>
#include <boost/pfr/core_name.hpp>
#include <array>
#include <string_view>
#include <cassert>
#include <cstring>
#include <memory>
#include <sqlite3.h>
#include <string>
using sqliteBlob = std::vector<std::byte>;

class sqliteORM {
private:
    sqlite3 *db;
    char buffer[4096];
    const std::string path;

    sqliteORM(std::string path, sqlite3 *db) : db(db),
                                            buffer(),
                                            path(std::move(path)) {
    }

    void error_process(int status) const {
        if (status != SQLITE_OK && status != SQLITE_DONE && status != SQLITE_ROW) {
            std::cerr << sqlite3_errmsg(db);
            throw std::runtime_error("sqlite3 error happened");
        }
    }

    //用于生成建表SQL的私有函数
    template<typename T>
    static std::string get_class_name() {
        return boost::typeindex::type_id<T>().pretty_name();
    }

    template<typename T, size_t N>
    static constexpr const char *get_column_type_name() {
        using column_type = boost::pfr::tuple_element_t<N, T>;
        if constexpr (std::is_same_v<column_type, int> || std::is_same_v<column_type, long>) return "int";
        else if constexpr (std::is_same_v<column_type, float> || std::is_same_v<column_type, double>) return "float";
        else if constexpr (std::is_same_v<column_type, std::string>) return "text";
        else if constexpr (std::is_same_v<column_type, sqliteBlob>) return "blob";
        else throw std::runtime_error("未知的sqlite数据类型");
    }

    template<typename T, size_t N>
    static void create_table_sql_format(char *buffer, int &pos, const char *col_name) {
        pos += sprintf(buffer + pos, "%s %s,\n", col_name, get_column_type_name<T, N>());
    }


    //用于生成插入数据SQL语句并将数据绑定到stmt
    template<typename T, size_t N>
    constexpr void bind_sqlite_data(sqlite3_stmt *stmt, const T &data) {
        using column_type = boost::pfr::tuple_element_t<N, T>;
        int ret = SQLITE_OK;
        if constexpr (std::is_same_v<column_type, int> || std::is_same_v<column_type, long> ||
                      std::is_same_v<column_type, unsigned int> || std::is_same_v<column_type, unsigned long>) {
            ret = sqlite3_bind_int(stmt, N + 1, boost::pfr::get<N>(data));
        } else if constexpr (std::is_same_v<column_type, float> || std::is_same_v<column_type, double>) {
            ret = sqlite3_bind_double(stmt, N + 1, boost::pfr::get<N>(data));
        } else if constexpr (std::is_same_v<column_type, std::string>) {
            ret = sqlite3_bind_text(stmt, N + 1, boost::pfr::get<N>(data).c_str(),
                                    static_cast<int>(boost::pfr::get<N>(data).size()),
                                    SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<column_type, sqliteBlob>) {
            ret = sqlite3_bind_blob(stmt, N + 1, boost::pfr::get<N>(data).data(),
                                    static_cast<int>(boost::pfr::get<N>(data).size()),
                                    nullptr);
        } else
            throw std::runtime_error("未知的sqlite数据类型");
        error_process(ret);
    }

    template<typename T>
    sqlite3_stmt *gen_insert_stmt() {
        constexpr std::array<std::string_view, boost::pfr::tuple_size_v<T> > field_names =
                boost::pfr::names_as_array<T>();
        constexpr auto field_count = field_names.size();
        int pos = sprintf(buffer, "INSERT INTO '%s' VALUES (", get_class_name<T>().c_str());
        for (auto i = 0; i < field_count; i++)
            pos += sprintf(buffer + pos, "?,");
        pos--;
        pos += sprintf(buffer + pos, ");");
        sqlite3_stmt *stmt;
        auto status = sqlite3_prepare_v2(db, buffer, pos, &stmt, nullptr);
        error_process(status);
        return stmt;
    }

    //从stmt获取数据
    template<typename T, size_t N>
    void get_data_from_stmt(sqlite3_stmt *stmt, T &data) {
        using column_type = boost::pfr::tuple_element_t<N, T>;
        int ret = SQLITE_OK;
        if constexpr (std::is_same_v<column_type, int> || std::is_same_v<column_type, unsigned int >) {
            boost::pfr::get<N, T>(data) = sqlite3_column_int(stmt, N);
        }else if constexpr (std::is_same_v<column_type, long> || std::is_same_v<column_type, unsigned long>) {
            boost::pfr::get<N, T>(data) = sqlite3_column_int64(stmt, N);
        } else if constexpr (std::is_same_v<column_type, float> || std::is_same_v<column_type, double>) {
            boost::pfr::get<N, T>(data) = sqlite3_column_double(stmt, N);
        } else if constexpr (std::is_same_v<column_type, std::string>) {
            boost::pfr::get<N, T>(data) = reinterpret_cast<const char*>(sqlite3_column_text(stmt, N));
        } else if constexpr (std::is_same_v<column_type, sqliteBlob>) {
            auto size = sqlite3_column_bytes(stmt, N);
            sqliteBlob blob(size);
            auto start = sqlite3_column_blob(stmt,N);
            memcpy(blob.data(),start,size);
            boost::pfr::get<N,T>(data) = blob;
        } else
            throw std::runtime_error("未知的sqlite数据类型");
    }

public:
    static std::unique_ptr<sqliteORM> connect(const std::string &path) {
        sqlite3 *db;
        auto status = sqlite3_open(path.c_str(), &db);
        if (status != SQLITE_OK) {
            fprintf(stderr, "无法连接数据库(%s)错误码为%d\n", path.c_str(), status);
            throw std::runtime_error("sqlite3_open failed");
        }
        return std::unique_ptr<sqliteORM>(new sqliteORM(path, db));
    }

    [[nodiscard]] const std::string &get_path() const {
        return path;
    }

    void exec(const std::string &sql) const {
        char *errmsg;
        auto status = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
        if (status != SQLITE_OK) {
            fprintf(stderr, "执行SQL语句失败(%s)错误码为%d,错误信息为:%s\n", sql.c_str(), status, errmsg);
            throw std::runtime_error("sqlite3_exec failed");
        }
    }

    template<typename T>
    void create_table() {
        constexpr std::array<std::string_view, boost::pfr::tuple_size_v<T> > field_names =
                boost::pfr::names_as_array<T>();
        constexpr auto field_count = field_names.size();
        int pos = sprintf(buffer, "CREATE TABLE IF NOT EXISTS '%s' (\n", get_class_name<T>().c_str());
        [this,&pos,&field_names]<size_t ... Is>(std::index_sequence<Is...>) {
            (create_table_sql_format<T, Is>(buffer, pos, field_names[Is].data()), ...);
        }(std::make_index_sequence<field_count>{});
        pos -= 2;
        pos += sprintf(buffer + pos, ");");
        exec(buffer);
    }

    template<typename T>
    void insert(const T &data) {
        constexpr std::array<std::string_view, boost::pfr::tuple_size_v<T> > field_names =
                boost::pfr::names_as_array<T>();
        constexpr auto field_count = field_names.size();
        auto stmt = gen_insert_stmt<T>();
        //绑定数据
        [this,stmt,&data]<size_t ... Is>(std::index_sequence<Is...>) {
            (bind_sqlite_data<T, Is>(stmt, data), ...);
        }(std::make_index_sequence<field_count>{});
        auto status = sqlite3_step(stmt);
        error_process(status);
        sqlite3_finalize(stmt);
    }

    template<typename T>
    void insert(const std::vector<T> &data) {
        constexpr std::array<std::string_view, boost::pfr::tuple_size_v<T> > field_names =
                boost::pfr::names_as_array<T>();
        constexpr auto field_count = field_names.size();
        sprintf(buffer, "BEGIN TRANSACTION;");
        exec(buffer);
        auto stmt = gen_insert_stmt<T>();
        for (auto &item: data) {
            //绑定数据
            [this,stmt,&item]<size_t ... Is>(std::index_sequence<Is...>) {
                (bind_sqlite_data<T, Is>(stmt, item), ...);
            }(std::make_index_sequence<field_count>{});
            auto status = sqlite3_step(stmt);
            error_process(status);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        sprintf(buffer, "COMMIT;");
        exec(buffer);
    }

    template<typename T>
    void query(std::vector<T> &data,const char * where = nullptr,const char* group_by = nullptr,const char * order_by = nullptr) {
        constexpr std::array<std::string_view, boost::pfr::tuple_size_v<T> > field_names =
               boost::pfr::names_as_array<T>();
        constexpr auto field_count = field_names.size();
        sqlite3_stmt *stmt;
        //生成用于查询的sql
        int pos = sprintf(buffer,"SELECT * FROM '%s'\n",get_class_name<T>().c_str());
        if (where)
            pos += sprintf(buffer + pos, "WHERE %s\n", where);
        if (group_by)
            pos += sprintf(buffer + pos, "GROUP BY %s\n", group_by);
        if (order_by)
            pos += sprintf(buffer + pos, "ORDER BY %s\n", order_by);
        buffer[pos - 1] = ';';
        buffer[pos] = 0;
        pos ++;
        auto status = sqlite3_prepare_v2(db,buffer,pos, &stmt, nullptr);
        error_process(status);
        //开始查询
        status = sqlite3_step(stmt);
        while (status == SQLITE_ROW) {
            //生成一个新的用于获取数据的对象
            data.emplace_back();
            auto &item = data.back();
            //从stmt中获取数据
            [this,stmt,&item]<size_t ...Is>(std::index_sequence<Is...>){
                (get_data_from_stmt<T,Is>(stmt,item),...);
            }(std::make_index_sequence<field_count>{});
            status = sqlite3_step(stmt);
        }
        error_process(status);
        sqlite3_finalize(stmt);
    }

    ~sqliteORM() {
        sqlite3_close(db);
    }
};
#endif //NETSAMPLE_SQLITEORM_H
