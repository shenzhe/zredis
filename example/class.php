<?php
    class zredis 
    {
        public function __construct()
        {

        }
        
        /**
         * 
         * @param string $ip
         * @param int $port
         * @param int $timeout
         */
        public function connect($ip="127.0.0.1", $port="6379", $timeout="3")
        {

        }

        public function pconnect($ip="127.0.0.1", $port="6379", $timeout="3")
        {

        }

        /**
         * 
         * @param int $time
         */
        public function setTimeout($time)
        {

        }


        public function getTimeout()
        {

        }
        
        /**
         * 
         * @param int $interval
         */
        public function setKeepAliveInterval($interval) 
        {

        }
        
        public function getKeepAliveInterval() 
        {

        }
        
        /**
         * 
         * @param int $bytes
         */
        public function setMaxReadBuf($bytes)
        {
            
        }
        
        public function getMaxReadBuf()
        {
            
        }
        
        /**
         * 
         * @param bool $flag 
         */
        public function setThrowExceptions($flag)
        {
            
        }
        
        public function getThrowExceptions()
        {
            
        }
        
        /**
         * 
         * @param  $cmd  ( string $cmd [, mixed $args [, mixed $... ]] )
         */
        public function sendRaw($cmd) 
        {
            //$zredis->sendRaw("ping");
            //$zredis->sendRaw("set", "key", "val");
            //$zredis->sendRaw("mget", "key", "key2");
        }
        
        /**
         * 
         * @param array $cmds
         */
        public function sendRawArray(array $cmds)
        {
            //$zredis->sendRawArray(["ping"])
        }
        
        public function appendRaw($cmd)
        {
            //$zredis->appendRaw("ping");
            //$zredis->appendRaw("set", "key", "val");
        }
        
        public function appendRawArray($cmds)
        {
            //$zredis->appendRaw(["ping"]);
            //$zredis->appendRaw(["set", "key", "val"]);
        }
        
        public function getReply()
        {
            
        }
        
        public function getLastError()
        {
            
        }

    }
