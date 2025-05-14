#ifndef __NEOLUX_NXARGS_HPP__
#define __NEOLUX_NXARGS_HPP__

#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <optional>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

namespace neolux
{

    class NxArgs
    {
    public:
        enum class ArgType
        {
            STRING,
            INT,
            FLOAT,
            BOOL
        };

        struct Argument
        {
            std::string name;
            std::optional<std::string> short_opt;
            std::string help;
            ArgType type;
            bool required = false;
            std::any default_value;
            bool is_flag = false;       // for --verbose style flags
            bool is_positional = false; // for positional arguments
        };

        NxArgs(const std::string &description = "") : description_(description) {}

        void add_argument(const std::string &name,
                          ArgType type = ArgType::STRING,
                          const std::optional<std::string> &short_opt = {},
                          const std::string &help = "",
                          bool required = false,
                          std::any default_value = {},
                          bool is_flag = false);

        void parse_args(int argc, char *argv[]);

        template <typename T>
        T get(const std::string &name) const;

        void print_help(const std::string &prog_name) const;

    private:
        std::string description_;
        std::vector<Argument> arguments_;
        std::unordered_map<std::string, std::any> parsed_values_;

        const Argument *find_argument(const std::string &key) const;
        std::string to_string(ArgType type) const;
    };

} // namespace neolux

#endif // __NEOLUX_NXARGS_HPP__
