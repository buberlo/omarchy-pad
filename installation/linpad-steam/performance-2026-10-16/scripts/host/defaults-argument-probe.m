#import <Foundation/Foundation.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        NSUserDefaults *d=NSUserDefaults.standardUserDefaults;
        NSString *key=@"fastMode.codeCacheMB";
        id val=[d objectForKey:key];
        NSDictionary *result=@{@"argumentValue":[d volatileDomainForName:NSArgumentDomain][key] ?: NSNull.null,
                              @"dictionaryContainsKey":@([d.dictionaryRepresentation.allKeys containsObject:key]),
                              @"integerValue":@([d integerForKey:key]),
                              @"valueType":val ? NSStringFromClass([val class]) : @"missing"};
        NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
        puts([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
    }
}
