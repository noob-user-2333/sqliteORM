std::unique_ptr<sqliteORM> connect(const std::string &path)；
  连接path路径指定的数据库文件
 
void create_table<T>()
  直接创建与T同名数据库表，其字段名与T成员变量名一致且成员变量类型将被转化为sqlite数据类型

void insert(const T &data)；
void insert(const std::vector<T> &data)；
   向与T同名数据库表中插入数据

void query(std::vector<T> &data,const char * where = nullptr,const char* group_by = nullptr,const char * order_by = nullptr)；
  从T同名数据库表中获取数据并保存到data，若查询语句中包含指定子句，则需传入对应子句字符串指针
