#!/usr/bin/env python3
"""
上传打包文件到 GitHub Release
"""
import os
import json
import urllib.request
import urllib.error
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))

# GitHub Release 配置
GITHUB_REPO = "xxxzhou/avc_library"

def get_version():
    try:
        result = subprocess.run(['git', 'describe', '--tags', '--abbrev=0'],
                                capture_output=True, text=True, cwd=PROJECT_ROOT)
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    return None

def get_github_token():
    """从 config/github.json 读取 GitHub token"""
    config_path = os.path.join(PROJECT_ROOT, 'assets', 'config', 'github.json')
    if not os.path.exists(config_path):
        return None
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            config = json.load(f)
            token = config.get('github', {}).get('token', '')
            if token and token != 'YOUR_GITHUB_TOKEN_HERE':
                return token
    except:
        pass
    return None

def upload_to_github(files, token, version):
    """上传到 GitHub Release"""
    tag_name = version
    release_name = f"AvaPlayer {version}"

    # 检查 release 是否存在
    api_url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/tags/{tag_name}"
    req = urllib.request.Request(api_url)
    req.add_header('Authorization', f'token {token}')
    req.add_header('Accept', 'application/vnd.github.v3+json')

    try:
        response = urllib.request.urlopen(req)
        release_data = json.loads(response.read().decode())
        release_id = release_data['id']
        upload_url = release_data['upload_url'].replace('{?name,label}', '')
        print(f"使用已存在的 release: {tag_name}")
    except urllib.error.HTTPError:
        # 创建新 release
        create_url = f"https://api.github.com/repos/{GITHUB_REPO}/releases"
        data = json.dumps({
            'tag_name': tag_name,
            'name': release_name,
            'body': f'AvaPlayer Release {version}',
            'draft': False,
            'prerelease': True
        }).encode('utf-8')

        req = urllib.request.Request(create_url, data=data, method='POST')
        req.add_header('Authorization', f'token {token}')
        req.add_header('Accept', 'application/vnd.github.v3+json')
        req.add_header('Content-Type', 'application/json')

        response = urllib.request.urlopen(req)
        release_data = json.loads(response.read().decode())
        release_id = release_data['id']
        upload_url = release_data['upload_url'].replace('{?name,label}', '')
        print(f"创建新 release: {tag_name}")

    # 上传文件
    def upload_file(file_path, content_type):
        file_name = os.path.basename(file_path)
        print(f"上传: {file_name}...")

        url = f"{upload_url}?name={file_name}"
        with open(file_path, 'rb') as f:
            data = f.read()

        req = urllib.request.Request(url, data=data, method='POST')
        req.add_header('Authorization', f'token {token}')
        req.add_header('Accept', 'application/vnd.github.v3+json')
        req.add_header('Content-Type', content_type)

        try:
            response = urllib.request.urlopen(req)
            result = json.loads(response.read().decode())
            print(f"  成功: {result.get('browser_download_url', '')}")
            return True
        except urllib.error.HTTPError as e:
            print(f"  失败: {e.code} {e.reason}")
            return False

    results = []
    for file_path, content_type in files:
        if os.path.exists(file_path):
            results.append(upload_file(file_path, content_type))

    return all(results)

def main():
    print("=" * 60)
    print("GitHub Release 上传工具")
    print("=" * 60)

    # 检查版本
    version = get_version()
    if not version:
        print("错误: 无法获取版本号，请先创建 git tag")
        return
    print(f"版本: {version}")

    # 检查 GitHub token
    token = get_github_token()
    if not token:
        print("错误: 未找到有效的 GitHub token")
        print("请在 assets/config/github.json 中配置 github.token")
        return

    # 查找打包文件
    dist_dir = os.path.join(PROJECT_ROOT, 'dist')
    files = []

    windows_zip = os.path.join(dist_dir, f'AvaPlayer-windows-x64-{version}.zip')
    if os.path.exists(windows_zip):
        files.append((windows_zip, 'application/zip'))
        print(f"找到: {windows_zip}")
    else:
        print(f"未找到: {windows_zip}")

    android_apk = os.path.join(dist_dir, f'AvaPlayer-android-arm64-{version}.apk')
    if os.path.exists(android_apk):
        files.append((android_apk, 'application/vnd.android.package-archive'))
        print(f"找到: {android_apk}")
    else:
        print(f"未找到: {android_apk}")

    if not files:
        print("错误: 没有找到可上传的文件")
        print("请先运行 pack_avaplayer.py 进行打包")
        return

    # 上传
    print()
    if upload_to_github(files, token, version):
        print()
        print("=" * 60)
        print("上传成功!")
        print(f"Release 页面: https://github.com/{GITHUB_REPO}/releases/tag/{version}")
        print("=" * 60)
    else:
        print("上传失败")

if __name__ == '__main__':
    main()
