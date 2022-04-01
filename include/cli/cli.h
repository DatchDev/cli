/*******************************************************************************
 * CLI - A simple command line interface.
 * Copyright (C) 2016-2021 Daniele Pallastrelli
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************/

#ifndef CLI_CLI_H
#define CLI_CLI_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <cctype> // std::isspace
#include <type_traits>
#include "colorprofile.h"
#include "detail/history.h"
#include "detail/split.h"
#include "detail/fromstring.h"
#include "historystorage.h"
#include "volatilehistorystorage.h"
#include <iostream>
#include <utility>

namespace cli
{

    // ********************************************************************

    template < typename T > struct TypeDesc { static const char* Name() { return ""; } };
    template <> struct TypeDesc< char > { static const char* Name() { return "<char>"; } };
    template <> struct TypeDesc< unsigned char > { static const char* Name() { return "<unsigned char>"; } };
    template <> struct TypeDesc< signed char > { static const char* Name() { return "<signed char>"; } };
    template <> struct TypeDesc< short > { static const char* Name() { return "<short>"; } };
    template <> struct TypeDesc< unsigned short > { static const char* Name() { return "<unsigned short>"; } };
    template <> struct TypeDesc< int > { static const char* Name() { return "<int>"; } };
    template <> struct TypeDesc< unsigned int > { static const char* Name() { return "<unsigned int>"; } };
    template <> struct TypeDesc< long > { static const char* Name() { return "<long>"; } };
    template <> struct TypeDesc< unsigned long > { static const char* Name() { return "<unsigned long>"; } };
    template <> struct TypeDesc< long long > { static const char* Name() { return "<long long>"; } };
    template <> struct TypeDesc< unsigned long long > { static const char* Name() { return "<unsigned long long>"; } };
    template <> struct TypeDesc< float > { static const char* Name() { return "<float>"; } };
    template <> struct TypeDesc< double > { static const char* Name() { return "<double>"; } };
    template <> struct TypeDesc< long double > { static const char* Name() { return "<long double>"; } };
    template <> struct TypeDesc< bool > { static const char* Name() { return "<bool>"; } };
    template <> struct TypeDesc< std::string > { static const char* Name() { return "<string>"; } };
    template <> struct TypeDesc< std::vector<std::string> > { static const char* Name() { return "<list of strings>"; } };

    // ********************************************************************

    // forward declarations
    class Menu;
    class CliSession;


    class Cli
    {

        // inner class to provide a global output stream
        class OutStream : public std::basic_ostream<char>, public std::streambuf
        {
        public:
            OutStream() : std::basic_ostream<char>(this)
            {
            }

            // std::streambuf overrides
            std::streamsize xsputn(const char* s, std::streamsize n) override
            {
                for (auto os: ostreams)
                    os->rdbuf()->sputn(s, n);
                return n;
            }
            int overflow(int c) override
            {
                for (auto os: ostreams)
                    *os << static_cast<char>(c);
                return c;
            }            

        private:
            friend class Cli;

            void Register(std::ostream& o)
            {
                ostreams.push_back(&o);
            }
            void UnRegister(std::ostream& o)
            {
                ostreams.erase(std::remove(ostreams.begin(), ostreams.end(), &o), ostreams.end());
            }

            std::vector<std::ostream*> ostreams;
        };
        // end inner class

    public:
        ~Cli() = default;
        // disable value semantics
        Cli(const Cli&) = delete;
        Cli& operator = (const Cli&) = delete;
        // enable move semantics
        Cli(Cli&&) = default;
        Cli& operator = (Cli&&) = default;

        /// \deprecated Use the @c Cli::Cli(std::unique_ptr<Menu>,std::unique_ptr<HistoryStorage>) 
        /// overload version and the method @c Cli::ExitAction instead
        [[deprecated("Use the other overload of Cli constructor and the method Cli::ExitAction instead")]]
        explicit Cli(
            std::unique_ptr<Menu>&& _rootMenu,
            std::function< void(std::ostream&)> _exitAction,
            std::unique_ptr<HistoryStorage>&& historyStorage = std::make_unique<VolatileHistoryStorage>()
        ) :
            globalHistoryStorage(std::move(historyStorage)),
            rootMenu(std::move(_rootMenu)),
            exitAction(std::move(_exitAction))
        {
        }

        /**
         * @brief Construct a new Cli object having a given root menu that contains the first level commands available.
         * 
         * @param _rootMenu is the @c Menu containing the first level commands available to the user.
         * @param historyStorage is the policy for the storage of the cli commands history. You must pass an istance of
         * a class derived from @c HistoryStorage. The library provides these policies:
         *   - @c VolatileHistoryStorage
         *   - @c FileHistoryStorage it's a persistent history. I.e., the command history is preserved after your application
         *     is restarted.
         * 
         * However, you can develop your own, just derive a class from @c HistoryStorage .
         */
        Cli(std::unique_ptr<Menu> _rootMenu, std::unique_ptr<HistoryStorage> historyStorage = std::make_unique<VolatileHistoryStorage>()) :
            globalHistoryStorage(std::move(historyStorage)),
            rootMenu(std::move(_rootMenu)),
            exitAction{}
        {
        }

        /**
         * @brief Add a global exit action that is called every time a session (local or remote) gets the "exit" command.
         * 
         * @param action the function to be called when a session exits, taking a @c std::ostream& parameter to write on that session console.
         */
        void ExitAction(const std::function< void(std::ostream&)>& action) { exitAction = action; }

        /**
         * @brief Add an handler that will be called when a @c std::exception (or derived) is thrown inside a command handler.
         * If an exception handler is not set, the exception will be logget on the session output stream.
         * 
         * @param handler the function to be called when an exception is thrown, taking a @c std::ostream& parameter to write on that session console
         * and the exception thrown.
         */
        void StdExceptionHandler(const std::function< void(std::ostream&, const std::string& cmd, const std::exception&) >& handler)
        {
            exceptionHandler = handler;
        }

        /**
         * @brief Get a global out stream object that can be used to print on every session currently connected (local and remote)
         * 
         * @return OutStream& the reference to the global out stream writing on every session console. 
         */
        static OutStream& cout()
        {
            static OutStream s;
            return s;
        }

    private:
        friend class CliSession;

        Menu* RootMenu() { return rootMenu.get(); }

        void ExitAction( std::ostream& out )
        {
            if ( exitAction )
                exitAction( out );
        }

        void StdExceptionHandler(std::ostream& out, const std::string& cmd, const std::exception& e)
        {
            if (exceptionHandler)
                exceptionHandler(out, cmd, e);
            else
                out << e.what() << '\n';
        }

        static void Register(std::ostream& o) { cout().Register(o); }

        static void UnRegister(std::ostream& o) { cout().UnRegister(o); }

        void StoreCommands(const std::vector<std::string>& cmds)
        {
            globalHistoryStorage->Store(cmds);
        }

        std::vector<std::string> GetCommands() const
        {
            return globalHistoryStorage->Commands();
        }

    private:
        std::unique_ptr<HistoryStorage> globalHistoryStorage;
        std::unique_ptr<Menu> rootMenu; // just to keep it alive
        std::function<void(std::ostream&)> exitAction;
        std::function<void(std::ostream&, const std::string& cmd, const std::exception& )> exceptionHandler;
    };

    // ********************************************************************

    class Command
    {
    public:
        explicit Command(std::string _name) : name(std::move(_name)), enabled(true) {}
        virtual ~Command() noexcept = default;

        // disable copy and move semantics
        Command(const Command&) = delete;
        Command(Command&&) = delete;
        Command& operator=(const Command&) = delete;
        Command& operator=(Command&&) = delete;

        virtual void Enable() { enabled = true; }
        virtual void Disable() { enabled = false; }
        virtual bool Exec(const std::vector<std::string>& cmdLine, CliSession& session) = 0;
        virtual void Help(std::ostream& out) const = 0;
        // Returns the collection of completions relatives to this command.
        // For simple commands, provides a base implementation that use the name of the command
        // for aggregate commands (i.e., Menu), the function is redefined to give the menu command
        // and the subcommand recursively
        virtual std::vector<std::string> GetCompletionRecursive(const std::string& line) const
        {
            if (!enabled) return {};
            if (name.rfind(line, 0) == 0) return {name}; // name starts_with line
            return {};
        }
    protected:
        const std::string& Name() const { return name; }
        bool IsEnabled() const { return enabled; }
    private:
        const std::string name;
        bool enabled;
    };

    // ********************************************************************

    // free utility function to get completions from a list of commands and the current line
    inline std::vector<std::string> GetCompletions(
        const std::shared_ptr<std::vector<std::shared_ptr<Command>>>& cmds,
        const std::string& currentLine)
    {
        std::vector<std::string> result;
        std::for_each(cmds->begin(), cmds->end(),
            [&currentLine,&result](const auto& cmd)
            {
                auto c = cmd->GetCompletionRecursive(currentLine);
                result.insert(
                    result.end(),
                    std::make_move_iterator(c.begin()),
                    std::make_move_iterator(c.end())
                );
            }
        );
        return result;
    }

    // ********************************************************************

    class CliSession
    {
    public:
        CliSession(Cli& _cli, std::ostream& _out, std::size_t historySize = 100);
        virtual ~CliSession() noexcept { Cli::UnRegister(out); }

        // disable value semantics
        CliSession(const CliSession&) = delete;
        CliSession& operator = (const CliSession&) = delete;
        // disable move semantics
        CliSession(CliSession&&) = delete;
        CliSession& operator = (CliSession&&) = delete;

        void Feed(const std::string& cmd);

        void Prompt();

        void Current(Menu* menu) { current = menu; }

        std::ostream& OutStream() { return out; }

        void Help() const;

        void Exit()
        {
            exitAction(out);
            cli.ExitAction(out);

            auto cmds = history.GetCommands();
            cli.StoreCommands(cmds);
        }

        void ExitAction(const std::function<void(std::ostream&)>& action)
        {
            exitAction = action;
        }

        void ShowHistory() const { history.Show(out); }

        std::string PreviousCmd(const std::string& line)
        {
            return history.Previous(line);
        }

        std::string NextCmd()
        {
            return history.Next();
        }

        std::vector<std::string> GetCompletions(std::string currentLine) const;

    private:

        Cli& cli;
        Menu* current;
        std::unique_ptr<Menu> globalScopeMenu;
        std::ostream& out;
        std::function< void(std::ostream&)> exitAction = []( std::ostream& ){};
        detail::History history;
    };

    // ********************************************************************

    class CmdHandler
    {
    public:
        using CmdVec = std::vector<std::shared_ptr<Command>>;
        CmdHandler() : descriptor(std::make_shared<Descriptor>()) {}
        CmdHandler(std::weak_ptr<Command> c, std::weak_ptr<CmdVec> v) :
            descriptor(std::make_shared<Descriptor>(c, v))
        {}
        void Enable() { if (descriptor) descriptor->Enable(); }
        void Disable() { if (descriptor) descriptor->Disable(); }
        void Remove() { if (descriptor) descriptor->Remove(); }
    private:
        struct Descriptor
        {
            Descriptor() = default;
            Descriptor(std::weak_ptr<Command> c, std::weak_ptr<CmdVec> v) :
                cmd(std::move(c)), cmds(std::move(v))
            {}
            void Enable()
            {
                if (auto c = cmd.lock())
                    c->Enable();
            }
            void Disable()
            {
                if(auto c = cmd.lock())
                    c->Disable();
            }
            void Remove()
            {
                auto scmd = cmd.lock();
                auto scmds = cmds.lock();
                if (scmd && scmds)
                {
                    auto i = std::find_if(
                        scmds->begin(),
                        scmds->end(),
                        [&](const auto& c){ return c.get() == scmd.get(); }
                    );
                    if (i != scmds->end())
                        scmds->erase(i);
                }
            }
            std::weak_ptr<Command> cmd;
            std::weak_ptr<CmdVec> cmds;
        };
        std::shared_ptr<Descriptor> descriptor;
    };

    // ********************************************************************

    class Menu : public Command
    {
    public:
        // disable value and move semantics
        Menu(const Menu&) = delete;
        Menu& operator = (const Menu&) = delete;
        Menu(Menu&&) = delete;
        Menu& operator = (Menu&&) = delete;

        Menu() : Command({}), parent(nullptr), description(), cmds(std::make_shared<Cmds>()) {}

        explicit Menu(const std::string& _name, std::string  desc = "(menu)") :
            Command(_name), parent(nullptr), description(std::move(desc)), cmds(std::make_shared<Cmds>())
        {}

        template <typename R, typename ... Args>
        CmdHandler Insert(const std::string& cmdName, R (*f)(std::ostream&, Args...), const std::string& help, const std::vector<std::string>& parDesc={});
        
        template <typename F>
        CmdHandler Insert(const std::string& cmdName, F f, const std::string& help = "", const std::vector<std::string>& parDesc={})
        {
            // dispatch to private Insert methods
            return Insert(cmdName, help, parDesc, f, &F::operator());
        }

        template <typename F>
        CmdHandler Insert(const std::string& cmdName, const std::vector<std::string>& parDesc, F f, const std::string& help = "")
        {
            // dispatch to private Insert methods
            return Insert(cmdName, help, parDesc, f, &F::operator());
        }

        CmdHandler Insert(std::unique_ptr<Command>&& cmd)
        {
            std::shared_ptr<Command> scmd(std::move(cmd));
            CmdHandler c(scmd, cmds);
            cmds->push_back(scmd);
            return c;
        }

        CmdHandler Insert(std::unique_ptr<Menu>&& menu)
        {
            std::shared_ptr<Menu> smenu(std::move(menu));
            CmdHandler c(smenu, cmds);
            smenu->parent = this;
            cmds->push_back(smenu);
            return c;
        }

        bool Exec(const std::vector<std::string>& cmdLine, CliSession& session) override
        {
            if (!IsEnabled())
                return false;
            if (cmdLine[0] == Name())
            {
                if (cmdLine.size() == 1)
                {
                    session.Current(this);
                    return true;
                }
                else
                {
                    // check also for subcommands
                    std::vector<std::string > subCmdLine(cmdLine.begin()+1, cmdLine.end());
                    for (auto& cmd: *cmds)
                        if (cmd->Exec( subCmdLine, session )) return true;
                }
            }
            return false;
        }

        bool ScanCmds(const std::vector<std::string>& cmdLine, CliSession& session)
        {
            if (!IsEnabled())
                return false;
            for (auto& cmd: *cmds)
                if (cmd->Exec(cmdLine, session))
                    return true;
            return (parent && parent->Exec(cmdLine, session));
        }

        std::string Prompt() const
        {
            return Name();
        }

        void MainHelp(std::ostream& out)
        {
            if (!IsEnabled()) return;
            for (const auto& cmd: *cmds)
                cmd->Help(out);
            if (parent != nullptr)
                parent->Help(out);
        }

        void Help(std::ostream& out) const override
        {
            if (!IsEnabled()) return;
            out << " - " << Name() << "\n\t" << description << "\n";
        }

        // returns:
        // - the completions of this menu command
        // - the recursive completions of subcommands
        // - the recursive completions of parent menu
        std::vector<std::string> GetCompletions(const std::string& currentLine) const
        {
            auto result = cli::GetCompletions(cmds, currentLine);
            if (parent != nullptr)
            {
                auto c = parent->GetCompletionRecursive(currentLine);
                result.insert(result.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
            }
            return result;
        }

        // returns:
        // - the completion of this menu command
        // - the recursive completions of the subcommands
        std::vector<std::string> GetCompletionRecursive(const std::string& line) const override
        {
            if (line.rfind(Name(), 0) == 0) // line starts_with Name()
            {
                auto rest = line;
                rest.erase(0, Name().size());
                // trim_left(rest);
                rest.erase(rest.begin(), std::find_if(rest.begin(), rest.end(), [](int ch) { return !std::isspace(ch); }));
                std::vector<std::string> result;
                for (const auto& cmd: *cmds)
                {
                    auto cs = cmd->GetCompletionRecursive(rest);
                    for (const auto& c: cs)
                        result.push_back(Name() + ' ' + c); // concat submenu with command
                }
                return result;
            }
            return Command::GetCompletionRecursive(line);
        }

    private:

        template <typename F, typename R, typename ... Args>
        CmdHandler Insert(const std::string& name, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, Args...) const);

        template <typename F, typename R>
        CmdHandler Insert(const std::string& name, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, const std::vector<std::string>&) const);

        template <typename F, typename R>
        CmdHandler Insert(const std::string& name, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, std::vector<std::string>) const);

        Menu* parent{ nullptr };
        const std::string description;
        // using shared_ptr instead of unique_ptr to get a weak_ptr
        // for the CmdHandler::Descriptor
        using Cmds = std::vector<std::shared_ptr<Command>>;
        std::shared_ptr<Cmds> cmds;
    };

    // ********************************************************************

    template <typename ... Args>
    struct Select;

    template <typename P, typename ... Args>
    struct Select<P, Args...>
    {
        template <typename F, typename InputIt>
        static void Exec(const F& f, InputIt first, InputIt last)
        {
            assert( first != last );
            assert( std::distance(first, last) == 1+sizeof...(Args) );
            const P p = detail::from_string<typename std::decay<P>::type>(*first);
            auto g = [&](auto ... pars){ f(p, pars...); };
            Select<Args...>::Exec(g, std::next(first), last);
        }
    };

    template <>
    struct Select<>
    {
        template <typename F, typename InputIt>
        static void Exec(const F& f, InputIt first, InputIt last)
        {
            // silence the unused warning in release mode when assert is disabled
            static_cast<void>(first);
            static_cast<void>(last);

            assert(first == last);
            
            f();
        }
    };

    template <typename ... Args>
    struct PrintDesc;

    template <typename P, typename ... Args>
    struct PrintDesc<P, Args...>
    {
        static void Dump(std::ostream& out)
        {
            out << " " << TypeDesc< typename std::decay<P>::type >::Name();
            PrintDesc<Args...>::Dump(out);
        }
    };

    template <>
    struct PrintDesc<>
    {
        static void Dump(std::ostream& /*out*/) {}
    };

    // *******************************************

    template <typename F, typename ... Args>
    class VariadicFunctionCommand : public Command
    {
    public:
        // disable value semantics
        VariadicFunctionCommand(const VariadicFunctionCommand&) = delete;
        VariadicFunctionCommand& operator = (const VariadicFunctionCommand&) = delete;

        VariadicFunctionCommand(
            const std::string& _name,
            F fun,
            std::string desc,
            std::vector<std::string> parDesc
        )
            : Command(_name), func(std::move(fun)), description(std::move(desc)), parameterDesc(std::move(parDesc))
        {
        }

        bool Exec(const std::vector< std::string >& cmdLine, CliSession& session) override
        {
            if (!IsEnabled()) return false;
            const std::size_t paramSize = sizeof...(Args);
            if (cmdLine.size() != paramSize+1) return false;
            if (Name() == cmdLine[0])
            {
                try
                {
                    auto g = [&](auto ... pars){ func( session.OutStream(), pars... ); };
                    Select<Args...>::Exec(g, std::next(cmdLine.begin()), cmdLine.end());
                }
                catch (std::bad_cast&)
                {
                    return false;
                }
                return true;
            }
            return false;
        }

        void Help(std::ostream& out) const override
        {
            if (!IsEnabled()) return;
            out << " - " << Name();
            if (parameterDesc.empty())
                PrintDesc<Args...>::Dump(out);
            for (auto& s: parameterDesc)
                out << " <" << s << '>';
            out << "\n\t" << description << "\n";
        }

    private:

        const F func;
        const std::string description;
        const std::vector<std::string> parameterDesc;
    };


    template <typename F>
    class FreeformCommand : public Command
    {
    public:
        // disable value semantics
        FreeformCommand(const FreeformCommand&) = delete;
        FreeformCommand& operator = (const FreeformCommand&) = delete;

        FreeformCommand(
            const std::string& _name,
            F fun,
            std::string desc,
            std::vector<std::string> parDesc
        )
            : Command(_name), func(std::move(fun)), description(std::move(desc)), parameterDesc(std::move(parDesc))
        {
        }

        bool Exec(const std::vector< std::string >& cmdLine, CliSession& session) override
        {
            if (!IsEnabled()) return false;
            assert(!cmdLine.empty());
            if (Name() == cmdLine[0])
            {
                func(session.OutStream(), std::vector<std::string>(std::next(cmdLine.begin()), cmdLine.end()));
                return true;
            }
            return false;
        }
        void Help(std::ostream& out) const override
        {
            if (!IsEnabled()) return;
            out << " - " << Name();
            if (parameterDesc.empty())
                PrintDesc<std::vector<std::string>>::Dump(out);            
            for (auto& s: parameterDesc)
                out << " <" << s << '>';
            out << "\n\t" << description << "\n";
        }

    private:

        const F func;
        const std::string description;
        const std::vector<std::string> parameterDesc;
    };


    // ********************************************************************

    // CliSession implementation

    inline CliSession::CliSession(Cli& _cli, std::ostream& _out, std::size_t historySize) :
            cli(_cli),
            current(cli.RootMenu()),
            globalScopeMenu(std::make_unique< Menu >()),
            out(_out),
            history(historySize)
        {
            history.LoadCommands(cli.GetCommands());

            Cli::Register(out);
            globalScopeMenu->Insert(
                "help",
                [this](std::ostream&){ Help(); },
                "This help message"
            );
            globalScopeMenu->Insert(
                "exit",
                [this](std::ostream&){ Exit(); },
                "Quit the session"
            );
#ifdef CLI_HISTORY_CMD
            globalScopeMenu->Insert(
                "history",
                [this](std::ostream&){ ShowHistory(); },
                "Show the history"
            );
#endif
        }

    inline void CliSession::Feed(const std::string& cmd)
    {
        std::vector<std::string> strs;
        detail::split(strs, cmd);
        if (strs.empty()) return; // just hit enter

        history.NewCommand(cmd); // add anyway to history

        try
        {

            // global cmds check
            bool found = globalScopeMenu->ScanCmds(strs, *this);

            // root menu recursive cmds check
            if (!found) found = current->ScanCmds(strs, *this);

            if (!found) // error msg if not found
                out << "wrong command: " << cmd << '\n';
        }
        catch(const std::exception& e)
        {
            cli.StdExceptionHandler(out, cmd, e);
        }
        catch(...)
        {
            out << "Cli. Unknown exception caught handling command line \""
                << cmd
                << "\"\n";
        }
    }

    inline void CliSession::Prompt()
    {
        out << beforePrompt
            << current->Prompt()
            << afterPrompt
            << "> "
            << std::flush;
    }

    inline void CliSession::Help() const
    {
        out << "Commands available:\n";
        globalScopeMenu->MainHelp(out);
        current -> MainHelp( out );
    }

    inline std::vector<std::string> CliSession::GetCompletions(std::string currentLine) const
    {
        // trim_left(currentLine);
        currentLine.erase(currentLine.begin(), std::find_if(currentLine.begin(), currentLine.end(), [](int ch) { return !std::isspace(ch); }));
        auto v1 = globalScopeMenu->GetCompletions(currentLine);
        auto v3 = current->GetCompletions(currentLine);
        v1.insert(v1.end(), std::make_move_iterator(v3.begin()), std::make_move_iterator(v3.end()));

        // removes duplicates (std::unique requires a sorted container)
        std::sort(v1.begin(), v1.end());
        auto ip = std::unique(v1.begin(), v1.end());
        v1.resize(static_cast<std::size_t>(std::distance(v1.begin(), ip)));

        return v1;
    }

    // Menu implementation

    template <typename R, typename ... Args>
    CmdHandler Menu::Insert(const std::string& cmdName, R (*f)(std::ostream&, Args...), const std::string& help, const std::vector<std::string>& parDesc)
    {
        using F = R (*)(std::ostream&, Args...);
        return Insert(std::make_unique<VariadicFunctionCommand<F, Args ...>>(cmdName, f, help, parDesc));
    }

    template <typename F, typename R, typename ... Args>
    CmdHandler Menu::Insert(const std::string& cmdName, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, Args...) const )
    {
        return Insert(std::make_unique<VariadicFunctionCommand<F, Args ...>>(cmdName, f, help, parDesc));
    }

    template <typename F, typename R>
    CmdHandler Menu::Insert(const std::string& cmdName, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, const std::vector<std::string>& args) const )
    {
        return Insert(std::make_unique<FreeformCommand<F>>(cmdName, f, help, parDesc));
    }

    template <typename F, typename R>
    CmdHandler Menu::Insert(const std::string& cmdName, const std::string& help, const std::vector<std::string>& parDesc, F& f, R (F::*)(std::ostream& out, std::vector<std::string> args) const )
    {
        return Insert(std::make_unique<FreeformCommand<F>>(cmdName, f, help, parDesc));
    }

} // namespace cli

#endif // CLI_CLI_H
