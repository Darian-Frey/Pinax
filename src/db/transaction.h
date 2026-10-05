#pragma once

namespace pinax::db {

class Connection;

// BEGIN IMMEDIATE on construction; ROLLBACK on destruction unless commit()
// was called. Any operation touching more than one row runs inside one
// (ARCHITECTURE.md §4).
class Transaction {
public:
    explicit Transaction(Connection& connection);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Connection& connection_;
    bool open_ = true;
};

} // namespace pinax::db
