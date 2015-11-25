# zredis
redis extension for php7

# 编译hiredis
  0) git clone https://github.com/redis/hiredis
  
  1) make
  
  2) make install

# 编译扩展
  0) phpize
  
  1) ./configure
  
  2) make
  
  3) make install
  
  5) 把zrediso.so 加入 php.ini
  
  
#demo
  ```php
    $redis = new zredis();
    $redis->connect();    //tcp方式，默认 127.0.0.1:6379
    //$redis->connect('/tmp/redis.sock');  //unix socket方式连接
    //$redis->connect('127.0.0.1', 6379, 3); //指定ip和端口，第三个参数是连接超时时间
    //$redis->pconnect();    //持久连接tcp方式，默认 127.0.0.1:6379
    //$redis->pconnect('/tmp/redis.sock');  //持久连接unix socket方式连接
    //$redis->pconnect('127.0.0.1', 6379, 3); //持久连接指定ip和端口，第三个参数是连接超时时间
    $redis->set("key", $val);  //redis指令参考：http://redis.io/commands
    echo $redis->get("key");
    $redis->close();   //关闭连接
  ```
  
  
#鸣谢：
  0) hiredis c client lib  （官方提供的c client库）
  
  1) Adam （在他的工作基础上进行完善） 
  
  
  
