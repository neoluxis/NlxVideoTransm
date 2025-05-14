#include "neolux/nxargs.hpp"

namespace neolux
{

    void NxArgs::add_argument(const std::string &name, ArgType type,
                              const std::optional<std::string> &short_opt,
                              const std::string &help,
                              bool required,
                              std::any default_value,
                              bool is_flag)
    {
        Argument arg{name, short_opt, help, type, required, default_value, is_flag, false};
        arguments_.push_back(arg);
    }

    void NxArgs::parse_args(int argc, char *argv[])
    {
        std::unordered_map<std::string, const Argument *> arg_lookup;
        for (const auto &arg : arguments_)
        {
            arg_lookup["--" + arg.name] = &arg;
            if (arg.short_opt)
                arg_lookup["-" + *arg.short_opt] = &arg;
        }

        for (int i = 1; i < argc; ++i)
        {
            std::string token = argv[i];

            if (token == "--help" || token == "-h")
            {
                print_help(argv[0]);
                std::exit(0);
            }

            auto it = arg_lookup.find(token);
            if (it != arg_lookup.end())
            {
                const Argument *arg = it->second;
                if (arg->is_flag)
                {
                    parsed_values_[arg->name] = true;
                }
                else
                {
                    if (i + 1 >= argc)
                        throw std::runtime_error("Missing value for " + token);
                    std::string value = argv[++i];
                    if (arg->type == ArgType::INT)
                        parsed_values_[arg->name] = std::stoi(value);
                    else if (arg->type == ArgType::FLOAT)
                        parsed_values_[arg->name] = std::stof(value);
                    else
                        parsed_values_[arg->name] = value;
                }
            }
            else
            {
                throw std::runtime_error("Unknown argument: " + token);
            }
        }

        // Fill defaults
        for (const auto &arg : arguments_)
        {
            if (!parsed_values_.count(arg.name))
            {
                if (arg.required && !arg.default_value.has_value())
                    throw std::runtime_error("Missing required argument: " + arg.name);
                parsed_values_[arg.name] = arg.default_value;
            }
        }
    }

    template <typename T>
    T NxArgs::get(const std::string &name) const
    {
        auto it = parsed_values_.find(name);
        if (it == parsed_values_.end())
            throw std::runtime_error("Argument not found: " + name);
        return std::any_cast<T>(it->second);
    }

    // Explicit template instantiation
    template int NxArgs::get<int>(const std::string &) const;
    template float NxArgs::get<float>(const std::string &) const;
    template std::string NxArgs::get<std::string>(const std::string &) const;
    template bool NxArgs::get<bool>(const std::string &) const;

    void NxArgs::print_help(const std::string &prog_name) const
    {
        std::cout << "Usage: " << prog_name << " [options]\n\n";

        // 打印描述文档
        if (!description_.empty())
        {
            std::cout << description_ << "\n\n";
        }

        std::cout << "Options:\n";

        for (const auto &arg : arguments_)
        {
            std::ostringstream opt_line;

            // 构建选项名
            if (arg.short_opt)
                opt_line << "-" << *arg.short_opt << ", ";
            else
                opt_line << "    ";
            opt_line << "--" << arg.name;

            // 类型提示
            switch (arg.type)
            {
            case ArgType::STRING:
                opt_line << " <str>";
                break;
            case ArgType::INT:
                opt_line << " <int>";
                break;
            case ArgType::FLOAT:
                opt_line << " <float>";
                break;
            case ArgType::BOOL:
                if (!arg.is_flag)
                    opt_line << " <bool>";
                break;
            }

            // 填充格式对齐
            std::cout << "  " << std::left << std::setw(25) << opt_line.str();

            // 描述 + 默认值
            std::cout << arg.help;
            if (arg.required)
                std::cout << " (required)";
            else if (arg.default_value.has_value())
            {
                std::cout << " [default: ";
                try
                {
                    if (arg.type == ArgType::INT)
                        std::cout << std::any_cast<int>(arg.default_value);
                    else if (arg.type == ArgType::FLOAT)
                        std::cout << std::any_cast<float>(arg.default_value);
                    else if (arg.type == ArgType::STRING)
                        std::cout << std::any_cast<std::string>(arg.default_value);
                    else if (arg.type == ArgType::BOOL)
                        std::cout << (std::any_cast<bool>(arg.default_value) ? "true" : "false");
                }
                catch (...)
                {
                    std::cout << "?";
                }
                std::cout << "]";
            }

            std::cout << "\n";
        }

        std::cout << "  -h, --help                 Show this help message and exit\n";
    }

} // namespace neolux
