volatile int counter = 0;

extern "C" void Reset_Handler()
{
    while(true)
    {
        counter++;
    }
}