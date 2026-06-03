RK3568 DTU Demo 配置包
适用系统: 银河麒麟 / Ubuntu (systemd)

目录说明:
1. config/rk3568-dtu-demo.json   板端运行配置
2. scripts/start.sh              启动脚本
3. scripts/install-service.sh    一键安装 systemd 服务
4. systemd/rk3568-dtu-demo.service 服务模板
5. bin/                          放置 rk3568-dtu-terminal-demo 二进制

部署步骤:
1. 把交叉编译后的 rk3568-dtu-terminal-demo 复制到 bin/ 目录
2. 整个 deploy/rk3568-dtu-package 拷到板子
3. 在板子上执行: sudo ./scripts/install-service.sh
4. 启动服务: sudo systemctl start rk3568-dtu-demo.service
5. 查看日志: sudo journalctl -u rk3568-dtu-demo.service -f

当前目标平台: 127.0.0.1:15020
当前串口: auto, 波特率: 9600, 从站: 1
