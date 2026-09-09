Pod::Spec.new do |s|
  # 基本信息
  s.name         = "avox"
  # 动态获取版本号，可从文件或环境变量读取
  s.version      = "1.0.0" 
  s.summary      = "A summary of avox library."
  s.description  = "A detailed description of avox library."
  s.homepage     = "https://example.com"
  s.license      = { :type => "MIT", :file => "LICENSE" }
  s.author       = { "Your Name" => "your_email@example.com" }   
  # 代码签名信息  
  development_team_id = 'F55URGYB7K'
  s.user_target_xcconfig = {
    'DEVELOPMENT_TEAM' => development_team_id,
    'CODE_SIGN_IDENTITY' => 'iPhone Developer',
    'CODE_SIGN_STYLE' => 'Automatic'    
  }

  # 支持的平台和最低版本
  s.platform     = :ios, "12.0"   
  s.source           = { :git => "", :tag => s.version.to_s } 
  # 源文件路径
  s.source_files = 'build/ios/avox/install/**/*.{h,m,cpp}' 
  # 头文件路径
  s.public_header_files = 'build/ios/avox/install/include/**/*.h'
  # 指定头文件映射目录，保持原有目录结构
  s.header_mappings_dir = 'build/ios/avox/install/include'
  # 库文件路径
  s.vendored_libraries = 'build/ios/avox/install/aarch64/Debug/*.{a,dylib}'
  # moltenvk 框架路径
  s.vendored_frameworks = 'build/ios/avox/install/aarch64/MoltenVK.framework'
  # 系统框架依赖
  s.frameworks = 'Security', 'CoreFoundation', 'CFNetwork', 'GLKit', 'OpenGLES', 'CoreMedia', 'CoreVideo', 'CoreAudio', 'AVFoundation', 'CoreGraphics', 'VideoToolbox', 'AudioToolbox','Foundation', 'CoreMotion', 'UIKit', 'QuartzCore','CoreBluetooth','GameController','IOSurface'
  s.libraries='z','bz2','iconv'
  # 编译选项
  s.requires_arc = true
  # 添加资源文件
  s.resources = 'build/ios/avox/install/aarch64/avox.bundle'
  # 添加其他依赖项，根据实际情况修改
  # s.dependency 'avox'
end