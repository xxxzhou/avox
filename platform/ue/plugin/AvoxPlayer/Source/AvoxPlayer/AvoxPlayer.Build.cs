// avox UE 插件运行时模块
// avox 第三方库由 platform/ue/deploy_ue.ps1 布置到插件 ThirdParty/avox/ 下

using UnrealBuildTool;
using System.IO;

public class AvoxPlayer : ModuleRules
{
	public AvoxPlayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", "CoreUObject", "Engine", "RHI", "RenderCore", "Projects"
			}
		);

		// ── avox 第三方库 (deploy_ue.ps1 部署到插件 ThirdParty/avox/) ──
		string avoxRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/avox"));
		string includeDir = Path.Combine(avoxRoot, "include");
		string libDir = Path.Combine(avoxRoot, "lib", "Win64");
		if (!Directory.Exists(Path.Combine(includeDir, "avox")) || !File.Exists(Path.Combine(libDir, "avox.lib")))
		{
			throw new BuildException(
				"AvoxPlayer: 缺少 avox 第三方库 (" + libDir + "\\avox.lib). " +
				"请在 avox 仓库运行 platform/ue/deploy_ue.ps1 -UeProject <工程路径> 部署");
		}
		PublicIncludePaths.Add(includeDir);
		PublicSystemLibraryPaths.Add(libDir);
		PublicAdditionalLibraries.Add(Path.Combine(libDir, "avox.lib"));

		// 延迟加载 avox.dll (运行时从插件 Binaries/Win64 解析, 部署脚本负责拷贝)
		PublicDelayLoadDLLs.Add("avox.dll");
		// avox 的运行时依赖 dll (FFmpeg/openssl/zlm 等), 打包时拷到目标 Binaries
		string[] runtimeDlls =
		{
			"avox.dll", "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll", "swresample-5.dll",
			"fdk-aac.dll", "faad-2.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "mk_api.dll"
		};
		foreach (string dll in runtimeDlls)
		{
			string dllPath = Path.Combine(libDir, dll);
			if (File.Exists(dllPath))
			{
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + dll, dllPath);
			}
		}
	}
}
