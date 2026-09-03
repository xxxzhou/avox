# git

## 常用操作

``` txt
# 当前状态 
git status

# 查看所有远程分支
git branch -r

# 或者查看所有分支（包括远程）
git branch -a

# 拉取当前本地分支对应的远程分支
git pull 这命令等于 git pull origin 当前分支名
# 推送当前本地分支到远程
git push 这命令等于 git push origin 当前分支名

# 远程分支日志
git log origin/main --oneline -5
git log --oneline -5

# 从远程分支创建本地分支并切换
git checkout -b 本地分支名 origin/远程分支名
git checkout -b dev origin/dev
```

## 子模块

[Git: submodule 子模块简明教程](https://zhuanlan.zhihu.com/p/404615843)

如果修改子模块.gitmodules里的url，比如http加载不了，改为ssl,需要删除.git/modules,.git/config里的子模块配置，然后重新git submodule update --init --recursive.

### 查看

git submodule status

### 添加

1. $ git submodule add git@github.com:knik0/faad2.git 3rdparty/faad2
2. $ git submodule update --init --recursive

### 移除

1. $ git submodule deinit -f 3rdparty/ct2
2. $ git rm 3rdparty/ct2
3. $ rm -rf .git/modules/3rdparty/ct2

## 更新submodle链接，https替换SSH

1. git config url."git@github.com:".insteadOf https://github.com/
2. .gitmodules文件里的https://github.com/替换为git@github.com:
3. git submodule sync  # 同步新URL
4. git submodule update --init --recursive  # 重新初始化子模块

### 指定分支

1. cd path/submodule
2. git checkout <branch_name>