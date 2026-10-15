#include "drivers/mysql/MySqlDriver.hpp"

#include "drivers/mysql/MySqlConnection.hpp"

namespace mydb::drivers::mysql {

std::unique_ptr<core::IConnection> MySqlDriver::createConnection(const core::ConnectionParams& params,
                                                                 core::ICredentialStore& credentials) const {
    return std::make_unique<MySqlConnection>(params, credentials, m_pluginDirectory);
}

} // namespace mydb::drivers::mysql
